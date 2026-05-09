#include "crawler.h"
#include "fetch.h"
#include "parse.h"
#include "robots.h"

#ifdef GUI_BUILD
#include "../gui/gui.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <strings.h>   /* strncasecmp */

/* ------------------------------------------------------------------ */
/* Context lifecycle                                                    */
/* ------------------------------------------------------------------ */

void init_crawler_context(crawler_context_t *ctx)
{
    init_queue(&ctx->queue);
    init_hash_table(&ctx->visited);

    pthread_mutex_init(&ctx->queue_lock,        NULL);
    pthread_cond_init (&ctx->queue_cond,        NULL);
    pthread_rwlock_init(&ctx->visited_rwlock,   NULL);
    pthread_mutex_init(&ctx->robots_cache_lock, NULL);
    pthread_mutex_init(&ctx->output_lock,       NULL);

    init_hash_table(&ctx->robots_hosts);
    ctx->robots_cache = NULL;
    ctx->gui_log = NULL;

    atomic_init(&ctx->active_threads, 0);
    atomic_init(&ctx->pages_failed,   0);
    atomic_init(&ctx->pages_skipped,  0);

    ctx->shutdown_flag = 0;
    ctx->pages_fetched = 0;

    /* Defaults (may be overridden by main() after init) */
    ctx->num_threads  = DEFAULT_NUM_THREADS;
    ctx->max_pages    = DEFAULT_MAX_PAGES;
    ctx->domain_scope = 0;
    ctx->verbose      = 0;
    ctx->seed_host    = NULL;
    ctx->output_file  = NULL;
}

void destroy_crawler_context(crawler_context_t *ctx)
{
    destroy_queue(&ctx->queue);
    destroy_hash_table(&ctx->visited);
    destroy_hash_table(&ctx->robots_hosts);

    /* Free robots cache entries */
    struct robots_cache_entry *rc = ctx->robots_cache;
    while (rc != NULL) {
        struct robots_cache_entry *next = rc->next;
        free(rc->host);
        robots_free(rc->rules);
        free(rc);
        rc = next;
    }
    ctx->robots_cache = NULL;

    free(ctx->seed_host);
    ctx->seed_host = NULL;

    pthread_mutex_destroy(&ctx->queue_lock);
    pthread_cond_destroy (&ctx->queue_cond);
    pthread_rwlock_destroy(&ctx->visited_rwlock);
    pthread_mutex_destroy(&ctx->robots_cache_lock);
    pthread_mutex_destroy(&ctx->output_lock);
}

/* ------------------------------------------------------------------ */
/* robots.txt cache lookup / populate                                   */
/* ------------------------------------------------------------------ */

/*
 * get_robots_rules – return cached robots rules for `host`, fetching
 * them on first access.  Returns NULL meaning "allow all" if fetch
 * failed or if no rules were defined.
 *
 * Must be called WITHOUT any other lock held (it calls fetch_page).
 */
static robots_rules_t *get_robots_rules(crawler_context_t *ctx,
                                         const char        *host,
                                         const char        *scheme)
{
    pthread_mutex_lock(&ctx->robots_cache_lock);

    /* Search existing cache */
    struct robots_cache_entry *entry = ctx->robots_cache;
    while (entry != NULL) {
        if (strcmp(entry->host, host) == 0) {
            robots_rules_t *rules = entry->rules;
            pthread_mutex_unlock(&ctx->robots_cache_lock);
            return rules;
        }
        entry = entry->next;
    }

    /* Not cached yet — release lock while fetching (network I/O) */
    pthread_mutex_unlock(&ctx->robots_cache_lock);

    /* Build origin string: "scheme://host" */
    size_t origin_len = strlen(scheme) + 3 + strlen(host) + 1;
    char  *origin     = malloc(origin_len);
    robots_rules_t *rules = NULL;
    if (origin != NULL) {
        snprintf(origin, origin_len, "%s://%s", scheme, host);
        rules = robots_fetch(origin, CRAWLER_USER_AGENT);
        free(origin);
    }

    /* Re-acquire lock and insert (another thread may have raced us) */
    pthread_mutex_lock(&ctx->robots_cache_lock);

    /* Re-check — if another thread already inserted, free our result */
    entry = ctx->robots_cache;
    while (entry != NULL) {
        if (strcmp(entry->host, host) == 0) {
            robots_free(rules);   /* discard ours */
            rules = entry->rules;
            pthread_mutex_unlock(&ctx->robots_cache_lock);
            return rules;
        }
        entry = entry->next;
    }

    /* Insert new cache entry */
    struct robots_cache_entry *new_entry = malloc(sizeof(*new_entry));
    if (new_entry != NULL) {
        new_entry->host  = strdup(host);
        new_entry->rules = rules;
        new_entry->next  = ctx->robots_cache;
        ctx->robots_cache = new_entry;
    }

    pthread_mutex_unlock(&ctx->robots_cache_lock);
    return rules;
}

/* ------------------------------------------------------------------ */
/* JSONL output                                                          */
/* ------------------------------------------------------------------ */

/*
 * write_jsonl_result – write one JSON line for the completed URL.
 * Thread-safe via output_lock.  No-op if output_file is NULL.
 */
static void write_jsonl_result(crawler_context_t *ctx,
                                const char        *url,
                                long               http_status,
                                size_t             links_found,
                                const char        *outcome)
{
    if (ctx->output_file == NULL) return;

    /* ISO-8601 timestamp */
    time_t    now = time(NULL);
    struct tm tm_buf;
    gmtime_r(&now, &tm_buf);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);

    pthread_mutex_lock(&ctx->output_lock);
    fprintf(ctx->output_file,
            "{\"url\":\"%s\",\"status\":%ld,\"links_found\":%zu,"
            "\"outcome\":\"%s\",\"timestamp\":\"%s\"}\n",
            url, http_status, links_found, outcome, ts);
    fflush(ctx->output_file);
    pthread_mutex_unlock(&ctx->output_lock);
}

/* ------------------------------------------------------------------ */
/* Thread-safe logging                                                  */
/* ------------------------------------------------------------------ */

/*
 * We use a dedicated print mutex to prevent interleaved output lines
 * from different worker threads.
 */
static pthread_mutex_t s_print_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * Thread-safe logging via a dedicated mutex.
 * We define LOG/LOG_ERR as GNU-extension macros (##__VA_ARGS__ swallows
 * the trailing comma when there are no variadic arguments).
 * GCC and Clang both support this even in pedantic mode when the
 * _GNU_SOURCE feature flag is present (implied by _POSIX_C_SOURCE >=
 * 200809L on glibc).  We silence the -Wpedantic diagnostic explicitly.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#define LOG(fmt, ...)  do { \
    pthread_mutex_lock(&s_print_lock); \
    printf(fmt, ##__VA_ARGS__); \
    pthread_mutex_unlock(&s_print_lock); \
} while (0)

#define LOG_ERR(fmt, ...)  do { \
    pthread_mutex_lock(&s_print_lock); \
    fprintf(stderr, fmt, ##__VA_ARGS__); \
    pthread_mutex_unlock(&s_print_lock); \
} while (0)
#pragma GCC diagnostic pop

/* ------------------------------------------------------------------ */
/* Step 1 – Acquire work from the queue                                 */
/* ------------------------------------------------------------------ */

/*
 * acquire_url – block until there is a URL to process, or until the
 * crawler should shut down.
 *
 * Returns a heap-allocated URL string (caller must free()), or NULL if
 * the thread should exit.  Must be called WITHOUT the queue_lock held.
 */
static char *acquire_url(crawler_context_t *ctx)
{
    pthread_mutex_lock(&ctx->queue_lock);

    while (1) {
        /* Shutdown requested (SIGINT or page cap reached) */
        if (ctx->shutdown_flag || (ctx->pages_fetched >= ctx->max_pages)) {
            pthread_mutex_unlock(&ctx->queue_lock);
            return NULL;
        }

        /* Is there work waiting? */
        if (!queue_is_empty(&ctx->queue)) {
            char *url = dequeue(&ctx->queue);

            /* Count this thread as active BEFORE releasing the lock */
            atomic_fetch_add(&ctx->active_threads, 1);

            pthread_mutex_unlock(&ctx->queue_lock);
            return url;
        }

        /*
         * No work, no shutdown yet.  If we are the last thread alive
         * and there is nothing in the queue, the crawl is truly done.
         */
        if (atomic_load(&ctx->active_threads) == 0) {
            ctx->shutdown_flag = 1;
            pthread_cond_broadcast(&ctx->queue_cond);
            pthread_mutex_unlock(&ctx->queue_lock);
            return NULL;
        }

        /* Other threads are still busy; wait for them to enqueue more */
        pthread_cond_wait(&ctx->queue_cond, &ctx->queue_lock);
    }
}

/* ------------------------------------------------------------------ */
/* Step 4 – Validate and register extracted links                       */
/* ------------------------------------------------------------------ */

/*
 * register_and_enqueue – for each extracted link, atomically check
 * whether it has been visited, mark it as visited if not, and add it
 * to the queue.  Uses double-checked locking with rwlock.
 */
static void register_and_enqueue(crawler_context_t *ctx,
                                  link_list_t       *links)
{
    for (size_t i = 0; i < links->count; i++) {
        const char *url = links->urls[i];

        /* --- Fast read-lock check --- */
        pthread_rwlock_rdlock(&ctx->visited_rwlock);
        int already_visited = hash_contains(&ctx->visited, url);
        pthread_rwlock_unlock(&ctx->visited_rwlock);

        if (already_visited) continue;

        /* --- Write-lock: double-check and insert --- */
        pthread_rwlock_wrlock(&ctx->visited_rwlock);

        if (!hash_contains(&ctx->visited, url)) {
            int inserted = hash_insert(&ctx->visited, url);
            pthread_rwlock_unlock(&ctx->visited_rwlock);

            if (inserted == 1) {
                pthread_mutex_lock(&ctx->queue_lock);
                if (ctx->pages_fetched < ctx->max_pages
                    && !ctx->shutdown_flag) {
                    enqueue(&ctx->queue, url);
                    pthread_cond_signal(&ctx->queue_cond);
                }
                pthread_mutex_unlock(&ctx->queue_lock);
            }
        } else {
            pthread_rwlock_unlock(&ctx->visited_rwlock);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Worker thread main loop                                              */
/* ------------------------------------------------------------------ */

static int is_static_asset(const char *url) {
    const char *exts[] = {
        ".css", ".js", ".png", ".jpg", ".jpeg", ".gif", ".svg", ".ico",
        ".webp", ".mp4", ".webm", ".woff", ".woff2", ".ttf", ".eot", ".pdf",
        ".zip", ".tar", ".gz", ".rar", ".mp3", ".wav", ".avi", ".mkv", NULL
    };
    const char *qmark = strchr(url, '?');
    const char *hash = strchr(url, '#');
    const char *end = url + strlen(url);
    if (qmark && qmark < end) end = qmark;
    if (hash && hash < end) end = hash;

    size_t len = end - url;
    for (int i = 0; exts[i] != NULL; i++) {
        size_t elen = strlen(exts[i]);
        if (len >= elen && strncasecmp(end - elen, exts[i], elen) == 0) {
            return 1;
        }
    }
    return 0;
}

void *worker_thread(void *arg)
{
    crawler_context_t *ctx = (crawler_context_t *)arg;

    while (1) {
        /* ---- Step 1: Get a URL to process ---- */
        char *url = acquire_url(ctx);
        if (url == NULL) break;   /* shutdown */

        if (is_static_asset(url)) {
            atomic_fetch_add(&ctx->pages_skipped, 1);
            
            pthread_mutex_lock(&ctx->queue_lock);
            atomic_fetch_sub(&ctx->active_threads, 1);
            pthread_cond_broadcast(&ctx->queue_cond);
            pthread_mutex_unlock(&ctx->queue_lock);
            free(url);
            continue;
        }

        /* ---- Step 1b: robots.txt check ---- */
        /* Extract host and scheme from the URL */
        char host_buf[256];
        const char *scheme = (strncmp(url, "https://", 8) == 0)
                             ? "https" : "http";

        if (url_extract_host(url, host_buf, sizeof(host_buf)) != NULL) {
            /* Determine the URL path (everything after the host) */
            const char *after_scheme = strstr(url, "://");
            const char *path = "/";
            if (after_scheme != NULL) {
                const char *host_end = strchr(after_scheme + 3, '/');
                if (host_end != NULL) path = host_end;
            }

            robots_rules_t *rules = get_robots_rules(ctx, host_buf, scheme);
            if (!robots_allowed(rules, path)) {
                if (ctx->verbose)
                    LOG("[robots]    %s\n", url);
                write_jsonl_result(ctx, url, 0, 0, "robots_blocked");
                atomic_fetch_add(&ctx->pages_skipped, 1);

                /* Still mark work done */
                pthread_mutex_lock(&ctx->queue_lock);
                atomic_fetch_sub(&ctx->active_threads, 1);
                pthread_cond_broadcast(&ctx->queue_cond);
                pthread_mutex_unlock(&ctx->queue_lock);

                free(url);
                continue;
            }
        }

        /* ---- Step 2: Fetch the page (no locks held during I/O) ---- */
        if (ctx->verbose)
            LOG("[fetching]  %s\n", url);

        fetch_result_t result;
        int fetch_ok = fetch_page(url, &result);

        /* ---- Step 3: Check Content-Type and parse HTML for links ---- */
        link_list_t links;
        init_link_list(&links);

        const char *outcome = "ok";
        int was_successful_html = 0;

        if (fetch_ok != 0) {
            outcome = "fetch_error";
            atomic_fetch_add(&ctx->pages_failed, 1);
        } else {
            /* Only parse text/html responses */
            int is_html = (result.content_type[0] == '\0') ||
                          (strncasecmp(result.content_type, "text/html", 9) == 0);

            if (!is_html) {
                outcome = "skipped_non_html";
                atomic_fetch_add(&ctx->pages_skipped, 1);
                free_fetch_result(&result);
            } else {
                was_successful_html = 1;
                const char *scope_host = ctx->domain_scope ? ctx->seed_host : NULL;
                extract_links(result.data, url, scope_host, &links);
                free_fetch_result(&result);
            }
        }

        /* ---- Step 4: Register new links and enqueue them ---- */
        register_and_enqueue(ctx, &links);

        /* ---- Step 5: Write JSONL result and GUI log ---- */
        write_jsonl_result(ctx, url,
                           result.http_status,
                           links.count,
                           outcome);

#ifdef GUI_BUILD
        if (ctx->gui_log != NULL) {
            gui_log_push(ctx->gui_log, url, result.http_status, links.count, outcome);
        }
#endif

        free_link_list(&links);

        /* ---- Step 6: Mark work as complete ---- */
        pthread_mutex_lock(&ctx->queue_lock);

        if (was_successful_html) {
            ctx->pages_fetched++;
            LOG("[done]      %s  (total: %d)\n", url, ctx->pages_fetched);
        } else {
            if (ctx->verbose) {
                LOG("[done]      %s  (skipped/failed)\n", url);
            }
        }

        if (ctx->pages_fetched >= ctx->max_pages) {
            ctx->shutdown_flag = 1;
        }

        /* Decrement active count while holding the lock so that the
         * shutdown check in acquire_url() sees a consistent state */
        atomic_fetch_sub(&ctx->active_threads, 1);

        /* Wake sleeping threads */
        pthread_cond_broadcast(&ctx->queue_cond);

        pthread_mutex_unlock(&ctx->queue_lock);

        free(url);
    }

    LOG("%s", "[thread]    exiting\n");
    return NULL;
}
