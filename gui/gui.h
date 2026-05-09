#pragma once

#include <pthread.h>

/* Forward declare crawler context */
typedef struct crawler_context crawler_context_t;

/* ------------------------------------------------------------------ */
/* GUI State and Logging Types                                        */
/* ------------------------------------------------------------------ */

#define GUI_LOG_CAPACITY 500

typedef enum {
    SCREEN_SETUP,
    SCREEN_DASHBOARD
} screen_t;

typedef struct {
    char url[2048];
    long http_status;
    int  links_found;
    char outcome[32];   /* "ok" | "fetch_error" | "skipped_non_html" | "robots_blocked" | "domain_scoped" */
} gui_log_entry_t;

typedef struct gui_log {
    gui_log_entry_t entries[GUI_LOG_CAPACITY];
    int             head;       /* next write index */
    int             count;      /* total items, capped at GUI_LOG_CAPACITY */
    pthread_mutex_t lock;
} gui_log_t;

typedef struct {
    screen_t screen;

    /* Setup input state */
    char url_input[2048];
    char output_input[256];
    int  url_error;         /* 1 = show red border */

    /* Config values */
    char threads_input[16];   /* default "4" */
    char pages_input[16];     /* default "200" */
    int domain_scope;       /* 0 or 1 */
    int verbose;            /* 0 or 1 */

    /* Dashboard state */
    float scroll_offset;
} gui_state_t;

/* ------------------------------------------------------------------ */
/* API                                                                  */
/* ------------------------------------------------------------------ */

void gui_log_init(gui_log_t *log);
void gui_log_push(gui_log_t *log, const char *url, long status, int links, const char *outcome);

/* Draw functions return 1 if a major state transition occurred (e.g. START clicked) */
int gui_draw_setup(gui_state_t *gs);
int gui_draw_dashboard(gui_state_t *gs, void *ctx_ptr, gui_log_t *log, double elapsed);
