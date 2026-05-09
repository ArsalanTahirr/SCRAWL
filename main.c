#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>   /* getopt */

#include <curl/curl.h>
#include <pthread.h>

#include "crawler.h"
#include "queue.h"
#include "hash.h"
#include "parse.h"

/* ------------------------------------------------------------------ */
/* SIGINT handler                                                        */
/* ------------------------------------------------------------------ */

/*
 * We keep a pointer to the crawler context so the signal handler can
 * set the shutdown flag.  Only written once before threads are created,
 * so no lock is needed for the pointer itself.
 */
static crawler_context_t *g_ctx = NULL;

static void sigint_handler(int sig)
{
    (void)sig;
    if (g_ctx == NULL) return;

    /* Signal all waiting threads to wake up and exit */
    pthread_mutex_lock(&g_ctx->queue_lock);
    g_ctx->shutdown_flag = 1;
    pthread_cond_broadcast(&g_ctx->queue_cond);
    pthread_mutex_unlock(&g_ctx->queue_lock);

    /* write() is async-signal-safe; printf is not, so we use it sparingly */
    const char msg[] = "\n[signal]    SIGINT received - shutting down...\n";
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
#pragma GCC diagnostic pop
}

/* ------------------------------------------------------------------ */
/* Usage                                                                */
/* ------------------------------------------------------------------ */

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [OPTIONS] <seed-url>\n"
        "\n"
        "Options:\n"
        "  -t <N>      Number of worker threads (default: %d)\n"
        "  -n <N>      Max pages to fetch      (default: %d)\n"
        "  -o <file>   Write JSONL results to file\n"
        "  -d          Restrict crawl to seed domain only\n"
        "  -v          Verbose: print each URL as it is fetched\n"
        "  -h          Show this help\n"
        "\n"
        "Example:\n"
        "  %s -t 8 -n 500 -d -o results.jsonl https://example.com\n",
        prog, DEFAULT_NUM_THREADS, DEFAULT_MAX_PAGES, prog);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                          */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    int         opt;
    int         num_threads  = DEFAULT_NUM_THREADS;
    int         max_pages    = DEFAULT_MAX_PAGES;
    int         domain_scope = 0;
    int         verbose      = 0;
    const char *output_path  = NULL;

    /* ---- Parse CLI options ---- */
    while ((opt = getopt(argc, argv, "t:n:o:dvh")) != -1) {
        switch (opt) {
        case 't':
            num_threads = atoi(optarg);
            if (num_threads <= 0) {
                fprintf(stderr, "Error: -t requires a positive integer\n");
                return EXIT_FAILURE;
            }
            break;
        case 'n':
            max_pages = atoi(optarg);
            if (max_pages <= 0) {
                fprintf(stderr, "Error: -n requires a positive integer\n");
                return EXIT_FAILURE;
            }
            break;
        case 'o':
            output_path = optarg;
            break;
        case 'd':
            domain_scope = 1;
            break;
        case 'v':
            verbose = 1;
            break;
        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        default:
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    /* ---- Positional: seed URL ---- */
    if (optind >= argc) {
        fprintf(stderr, "Error: seed URL required\n\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    const char *seed_url = argv[optind];

    /* Validate scheme */
    if (strncmp(seed_url, "http://",  7) != 0 &&
        strncmp(seed_url, "https://", 8) != 0) {
        fprintf(stderr,
                "Error: seed URL must start with http:// or https://\n"
                "       Got: %s\n", seed_url);
        return EXIT_FAILURE;
    }

    /* ---- Banner ---- */
    printf("=== Multithreaded Web Crawler ===\n");
    printf("Seed URL     : %s\n", seed_url);
    printf("Threads      : %d\n", num_threads);
    printf("Page limit   : %d\n", max_pages);
    printf("Domain-scope : %s\n", domain_scope ? "yes" : "no");
    printf("JSONL output : %s\n", output_path  ? output_path : "(none)");
    printf("=================================\n\n");

    /* ---- 1. Global libcurl initialisation ---- */
    CURLcode curl_init_result = curl_global_init(CURL_GLOBAL_ALL);
    if (curl_init_result != CURLE_OK) {
        fprintf(stderr, "curl_global_init() failed: %s\n",
                curl_easy_strerror(curl_init_result));
        return EXIT_FAILURE;
    }

    /* ---- 2. Initialise crawler context ---- */
    crawler_context_t ctx;
    init_crawler_context(&ctx);
    g_ctx = &ctx;

    /* Apply runtime config */
    ctx.num_threads  = num_threads;
    ctx.max_pages    = max_pages;
    ctx.domain_scope = domain_scope;
    ctx.verbose      = verbose;

    /* Extract seed host for domain-scoping */
    char host_buf[256];
    if (url_extract_host(seed_url, host_buf, sizeof(host_buf)) != NULL) {
        ctx.seed_host = strdup(host_buf);
    }

    char *actual_output_path = NULL;
    /* Open JSONL output file */
    if (output_path != NULL) {
        actual_output_path = strdup(output_path);
        size_t len = strlen(actual_output_path);
        if (len < 6 || strcmp(actual_output_path + len - 6, ".jsonl") != 0) {
            actual_output_path = realloc(actual_output_path, len + 7);
            strcat(actual_output_path, ".jsonl");
        }

        ctx.output_file = fopen(actual_output_path, "w");
        if (ctx.output_file == NULL) {
            perror("fopen output file");
            free(actual_output_path);
            destroy_crawler_context(&ctx);
            curl_global_cleanup();
            return EXIT_FAILURE;
        }
    }

    /* ---- 3. Install SIGINT handler ---- */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);

    /* ---- 4. Seed the queue and visited registry ---- */
    pthread_rwlock_wrlock(&ctx.visited_rwlock);
    hash_insert(&ctx.visited, seed_url);
    pthread_rwlock_unlock(&ctx.visited_rwlock);

    enqueue(&ctx.queue, seed_url);

    /* ---- 5. Spawn worker threads ---- */
    pthread_t threads[num_threads];
    int threads_created = 0;

    for (int i = 0; i < num_threads; i++) {
        int rc = pthread_create(&threads[i], NULL, worker_thread, &ctx);
        if (rc != 0) {
            fprintf(stderr, "pthread_create() failed for thread %d: %s\n",
                    i, strerror(rc));
            /* Signal already-running threads to stop */
            pthread_mutex_lock(&ctx.queue_lock);
            ctx.shutdown_flag = 1;
            pthread_cond_broadcast(&ctx.queue_cond);
            pthread_mutex_unlock(&ctx.queue_lock);
            break;
        }
        threads_created++;
    }

    /* ---- 6. Record start time ---- */
    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    /* ---- 7. Wait for all CREATED threads to finish ---- */
    for (int i = 0; i < threads_created; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &t_end);
    double elapsed = (t_end.tv_sec  - t_start.tv_sec) +
                     (t_end.tv_nsec - t_start.tv_nsec) / 1e9;

    /* ---- 8. Report summary ---- */
    printf("\n=== Crawl Complete ===\n");
    printf("Pages fetched  : %d\n",  ctx.pages_fetched);
    printf("Pages failed   : %d\n",  atomic_load(&ctx.pages_failed));
    printf("Pages skipped  : %d\n",  atomic_load(&ctx.pages_skipped));
    printf("URLs visited   : %zu\n", ctx.visited.count);
    printf("Elapsed time   : %.2f s\n", elapsed);
    if (actual_output_path)
        printf("Results file   : %s\n", actual_output_path);
    printf("======================\n");

    /* ---- 9. Cleanup ---- */
    free(actual_output_path);
    if (ctx.output_file != NULL) {
        fclose(ctx.output_file);
        ctx.output_file = NULL;
    }

    destroy_crawler_context(&ctx);
    curl_global_cleanup();

    return EXIT_SUCCESS;
}
