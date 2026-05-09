#include <raylib.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#include "gui.h"
#include "../include/crawler.h"
#include "../include/queue.h"
#include "../include/hash.h"
#include "../include/parse.h"

/* ------------------------------------------------------------------ */
/* Global State                                                         */
/* ------------------------------------------------------------------ */

static crawler_context_t g_ctx;
static gui_log_t         g_log;
static pthread_t         g_threads[16];
static int               g_threads_created = 0;
static double            g_start_time = 0;
Font                     g_font;

/* ------------------------------------------------------------------ */
/* Crawler Lifecycle Helpers                                            */
/* ------------------------------------------------------------------ */

static void start_crawl(gui_state_t *gs) {
    gui_log_init(&g_log);
    init_crawler_context(&g_ctx);

    g_ctx.gui_log      = &g_log;
    g_ctx.num_threads  = atoi(gs->threads_input);
    if (g_ctx.num_threads <= 0) g_ctx.num_threads = 1;
    g_ctx.max_pages    = atoi(gs->pages_input);
    if (g_ctx.max_pages <= 0) g_ctx.max_pages = 10;
    
    g_ctx.domain_scope = gs->domain_scope;
    g_ctx.verbose      = gs->verbose;

    if (strlen(gs->output_input) > 0) {
        g_ctx.output_file = fopen(gs->output_input, "w");
    }

    char host_buf[256];
    if (url_extract_host(gs->url_input, host_buf, sizeof(host_buf)) != NULL) {
        g_ctx.seed_host = strdup(host_buf);
    }

    pthread_rwlock_wrlock(&g_ctx.visited_rwlock);
    hash_insert(&g_ctx.visited, gs->url_input);
    pthread_rwlock_unlock(&g_ctx.visited_rwlock);

    enqueue(&g_ctx.queue, gs->url_input);

    g_threads_created = 0;
    for (int i = 0; i < g_ctx.num_threads; i++) {
        if (pthread_create(&g_threads[i], NULL, worker_thread, &g_ctx) == 0) {
            g_threads_created++;
        }
    }

    g_start_time = GetTime();
}

static void cleanup_crawl(void) {
    if (g_threads_created > 0) {
        pthread_mutex_lock(&g_ctx.queue_lock);
        g_ctx.shutdown_flag = 1;
        pthread_cond_broadcast(&g_ctx.queue_cond);
        pthread_mutex_unlock(&g_ctx.queue_lock);

        for (int i = 0; i < g_threads_created; i++) {
            pthread_join(g_threads[i], NULL);
        }
        g_threads_created = 0;
    }

    if (g_ctx.output_file) {
        fclose(g_ctx.output_file);
        g_ctx.output_file = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Entry Point                                                          */
/* ------------------------------------------------------------------ */

int main(void) {
    curl_global_init(CURL_GLOBAL_ALL);

    // SetConfigFlags(FLAG_WINDOW_RESIZABLE); // Removed resizing
    InitWindow(1600, 900, "SCRAWL - Multithreaded Web Crawler");
    SetTargetFPS(60);

    /* Load the custom font */
    g_font = LoadFontEx("assets/JetBrainsMono-Regular.ttf", 120, 0, 0);
    if (g_font.texture.id > 0) {
        SetTextureFilter(g_font.texture, TEXTURE_FILTER_BILINEAR);
    }

    gui_state_t gs;
    memset(&gs, 0, sizeof(gs));
    gs.screen      = SCREEN_SETUP;
    strcpy(gs.threads_input, "4");
    strcpy(gs.pages_input, "1");
    strcpy(gs.url_input, "https://example.com");

    while (!WindowShouldClose()) {
        BeginDrawing();
        ClearBackground((Color){ 18, 21, 28, 255 }); // C_BG

        if (gs.screen == SCREEN_SETUP) {
            if (gui_draw_setup(&gs)) {
                // Auto-append .jsonl
                int len = strlen(gs.output_input);
                if (len > 0) {
                    if (len < 6 || strcmp(gs.output_input + len - 6, ".jsonl") != 0) {
                        if ((long unsigned int)(len + 6) < sizeof(gs.output_input)) {
                            strcat(gs.output_input, ".jsonl");
                        }
                    }
                }

                start_crawl(&gs);
                gs.screen = SCREEN_DASHBOARD;
                gs.scroll_offset = 0;
            }
        } else if (gs.screen == SCREEN_DASHBOARD) {
            double elapsed = GetTime() - g_start_time;
            
            if (g_ctx.shutdown_flag || (g_ctx.pages_fetched >= g_ctx.max_pages) ||
                (atomic_load(&g_ctx.active_threads) == 0 && queue_is_empty(&g_ctx.queue))) {
            }

            if (gui_draw_dashboard(&gs, &g_ctx, &g_log, elapsed)) {
                cleanup_crawl();
                gs.screen = SCREEN_SETUP;
            }
        }
        
        EndDrawing();
    }

    if (gs.screen == SCREEN_DASHBOARD) {
        cleanup_crawl();
    }

    if (g_font.texture.id > 0) UnloadFont(g_font);
    CloseWindow();
    curl_global_cleanup();

    return 0;
}
