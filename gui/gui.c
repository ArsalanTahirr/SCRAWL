#include "gui.h"
#include "../include/crawler.h"
#include <raylib.h>

extern Font g_font;

#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/* Theme Colors                                                         */
/* ------------------------------------------------------------------ */

static const Color C_BG       = { 18,  21,  28, 255 };
static const Color C_SURFACE  = { 26,  31,  41, 255 };  
static const Color C_BORDER   = { 48,  54,  61, 255 };
static const Color C_ACCENT   = { 78, 163, 255, 255 };
static const Color C_GREEN    = { 63, 185,  80, 255 };
static const Color C_RED      = { 248, 81,  73, 255 };
static const Color C_ORANGE   = { 227, 179, 65, 255 };
static const Color C_TEXT_PRI = { 230, 237, 243, 255 };
static const Color C_TEXT_MUT = { 139, 148, 158, 255 };

/* ------------------------------------------------------------------ */
/* GUI Log Implementation                                               */
/* ------------------------------------------------------------------ */

void gui_log_init(gui_log_t *log) {
    log->head = 0;
    log->count = 0;
    pthread_mutex_init(&log->lock, NULL);
}

void gui_log_push(gui_log_t *log, const char *url, long status, int links, const char *outcome) {
    pthread_mutex_lock(&log->lock);

    int idx = log->head;
    strncpy(log->entries[idx].url, url, sizeof(log->entries[idx].url) - 1);
    log->entries[idx].url[sizeof(log->entries[idx].url) - 1] = '\0';
    log->entries[idx].http_status = status;
    log->entries[idx].links_found = links;
    strncpy(log->entries[idx].outcome, outcome, sizeof(log->entries[idx].outcome) - 1);
    log->entries[idx].outcome[sizeof(log->entries[idx].outcome) - 1] = '\0';

    log->head = (log->head + 1) % GUI_LOG_CAPACITY;
    if (log->count < GUI_LOG_CAPACITY) log->count++;

    pthread_mutex_unlock(&log->lock);
}

/* ------------------------------------------------------------------ */
/* Drawing Helpers                                                      */
/* ------------------------------------------------------------------ */

static void draw_text(const char *text, float x, float y, float size, Color color) {
    if (g_font.texture.id > 0) {
        DrawTextEx(g_font, text, (Vector2){x, y}, size, 1.0f, color);
    } else {
        DrawText(text, (int)x, (int)y, (int)size, color);
    }
}

static float measure_text(const char *text, float size) {
    if (g_font.texture.id > 0) {
        return MeasureTextEx(g_font, text, size, 1.0f).x;
    }
    return (float)MeasureText(text, (int)size);
}

static bool draw_button(Rectangle rect, const char *text, Color bg, Color fg) {
    Vector2 mouse = GetMousePosition();
    bool hover = CheckCollisionPointRec(mouse, rect);
    bool clicked = hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);

    Color current_bg = hover ? (Color){ bg.r/1.2, bg.g/1.2, bg.b/1.2, bg.a } : bg;
    
    DrawRectangleRec(rect, current_bg);
    DrawRectangleLinesEx(rect, 2, hover ? C_BG : current_bg);

    float tw = measure_text(text, 48);
    draw_text(text, rect.x + (rect.width - tw)/2, rect.y + (rect.height - 48)/2, 48, hover ? fg : C_BG);

    return clicked;
}

static bool draw_input_box(Rectangle rect, char *buffer, int max_len, bool active, bool error, bool numeric_only) {
    Vector2 mouse = GetMousePosition();
    bool hover = CheckCollisionPointRec(mouse, rect);
    bool clicked = hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);

    Color border = error ? C_RED : (active ? C_ACCENT : C_BORDER);
    DrawRectangleRec(rect, C_SURFACE);
    DrawRectangleLinesEx(rect, 2, border);

    float font_sz = 40.0f; 
    draw_text(buffer, rect.x + 15, rect.y + (rect.height - font_sz)/2, font_sz, C_TEXT_PRI);

    if (active) {
        float tw = measure_text(buffer, font_sz);
        
        // Blinking Cursor
        if ((int)(GetTime() * 2) % 2 == 0) {
            DrawRectangle(rect.x + 17 + tw, rect.y + 12, 12, rect.height - 24, C_ACCENT);
        }

        // Standard typing
        int key = GetCharPressed();
        while (key > 0) {
            if ((key >= 32) && (key <= 125)) {
                if ((int)strlen(buffer) < max_len - 1) {
                    if (!numeric_only || isdigit(key)) {
                        int len = strlen(buffer);
                        buffer[len] = (char)key;
                        buffer[len+1] = '\0';
                    }
                }
            }
            key = GetCharPressed();
        }

        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
            int len = strlen(buffer);
            if (len > 0) buffer[len-1] = '\0';
        }
    }
    return clicked;
}

static bool draw_checkbox(Rectangle rect, int *val, const char *label) {
    Vector2 mouse = GetMousePosition();
    bool hover = CheckCollisionPointRec(mouse, rect);
    if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        *val = !(*val);
        return true;
    }

    DrawRectangleRec(rect, C_SURFACE);
    DrawRectangleLinesEx(rect, 2, hover ? C_ACCENT : C_BORDER);

    if (*val) {
        DrawRectangle(rect.x + 8, rect.y + 8, rect.width - 16, rect.height - 16, C_ACCENT);
    }

    draw_text(label, rect.x + rect.width + 15, rect.y + 5, 40, C_TEXT_PRI);
    return hover;
}

static void draw_stat_card(Rectangle rect, const char *label, int val, Color color) {
    DrawRectangleRec(rect, C_SURFACE);
    DrawRectangleLinesEx(rect, 2, C_BORDER);

    draw_text(label, rect.x + 20, rect.y + 20, 36, C_TEXT_MUT);

    char val_str[32];
    sprintf(val_str, "%d", val);
    draw_text(val_str, rect.x + 20, rect.y + 70, 100, color);
}

/* ------------------------------------------------------------------ */
/* Screens                                                              */
/* ------------------------------------------------------------------ */

static int active_input = 1; // 1=url, 2=threads, 3=pages, 4=output

int gui_draw_setup(gui_state_t *gs) {
    int start_clicked = 0;

    int sw = 1600;
    int sh = 900;
    int cx = sw / 2;

    int form_w = 1200;
    int lx = cx - form_w / 2;

    int ty = sh * 0.08f;

    // Title
    draw_text("SCRAWL", lx, ty, 120, C_TEXT_PRI);
    draw_text("v1.0  Multithreaded Web Crawler", lx + 450, ty + 60, 40, C_TEXT_MUT);
    ty += 140;

    // Handle Tab Focus
    if (IsKeyPressed(KEY_TAB)) {
        active_input++;
        if (active_input > 4) active_input = 1;
    }

    // Clicked outside inputs
    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
        active_input = 0;
    }

    // Seed URL
    draw_text("Seed URL", lx, ty, 40, C_TEXT_MUT);
    Rectangle url_rect = { lx, ty + 50, form_w, 70 };
    if (draw_input_box(url_rect, gs->url_input, sizeof(gs->url_input), active_input == 1, gs->url_error, false)) active_input = 1;
    ty += 150;

    // Threads and Max Pages Row
    int half_w = (form_w - 40) / 2;
    draw_text("Worker Threads", lx, ty, 40, C_TEXT_MUT);
    Rectangle threads_rect = { lx, ty + 50, half_w, 70 };
    if (draw_input_box(threads_rect, gs->threads_input, sizeof(gs->threads_input), active_input == 2, false, true)) active_input = 2;

    int px = lx + half_w + 40;
    draw_text("Max Pages", px, ty, 40, C_TEXT_MUT);
    Rectangle pages_rect = { px, ty + 50, half_w, 70 };
    if (draw_input_box(pages_rect, gs->pages_input, sizeof(gs->pages_input), active_input == 3, false, true)) active_input = 3;
    ty += 150;

    // Output File
    draw_text("Output JSONL File (optional)", lx, ty, 40, C_TEXT_MUT);
    Rectangle out_rect = { lx, ty + 50, form_w, 70 };
    if (draw_input_box(out_rect, gs->output_input, sizeof(gs->output_input), active_input == 4, false, false)) active_input = 4;
    ty += 150;

    // Checkboxes
    draw_checkbox((Rectangle){ lx, ty, 50, 50 }, &gs->domain_scope, "Domain scope only");
    draw_checkbox((Rectangle){ lx + 550, ty, 50, 50 }, &gs->verbose, "Verbose terminal logging");
    ty += 90;

    // Start Button
    Rectangle btn_rect = { lx, ty, form_w, 100 };
    if (draw_button(btn_rect, "START CRAWL", C_ACCENT, C_TEXT_PRI) || (active_input > 0 && IsKeyPressed(KEY_ENTER))) {
        if (strncmp(gs->url_input, "http://", 7) != 0 && strncmp(gs->url_input, "https://", 8) != 0) {
            gs->url_error = 1;
        } else {
            start_clicked = 1;
        }
    }

    return start_clicked;
}

int gui_draw_dashboard(gui_state_t *gs, void *ctx_ptr, gui_log_t *log, double elapsed) {
    int back_clicked = 0;
    crawler_context_t *ctx = (crawler_context_t *)ctx_ptr;

    int sw = 1600;
    int sh = 900;

    int lx = sw * 0.05f;
    int form_w = sw * 0.9f;
    int ty = 40;

    // Header
    draw_text("SCRAWL", lx, ty, 80, C_TEXT_PRI);

    int is_running = !ctx->shutdown_flag && (ctx->pages_fetched < ctx->max_pages) && 
                     (atomic_load(&ctx->active_threads) > 0 || !queue_is_empty(&ctx->queue));

    if (is_running) {
        if ((int)(GetTime() * 2) % 2 == 0) DrawCircle(lx + 320, ty + 40, 20, C_GREEN);
        draw_text("CRAWLING", lx + 360, ty + 12, 60, C_GREEN);
    } else {
        draw_text("DONE", lx + 340, ty + 12, 60, C_ACCENT);
    }

    char time_str[64];
    int m = (int)elapsed / 60;
    int s = (int)elapsed % 60;
    sprintf(time_str, "%02d:%02d", m, s);
    draw_text(time_str, lx + 680, ty + 12, 60, C_TEXT_MUT);

    // Stop / New Crawl button
    if (is_running) {
        if (draw_button((Rectangle){ lx + form_w - 200, ty - 5, 200, 80 }, "STOP", C_RED, C_TEXT_PRI)) {
            pthread_mutex_lock(&ctx->queue_lock);
            ctx->shutdown_flag = 1;
            pthread_cond_broadcast(&ctx->queue_cond);
            pthread_mutex_unlock(&ctx->queue_lock);
        }
    } else {
        if (draw_button((Rectangle){ lx + form_w - 300, ty - 5, 300, 80 }, "NEW CRAWL", C_ACCENT, C_TEXT_PRI)) {
            back_clicked = 1;
        }
    }
    ty += 110;

    // Stats
    int fetched = ctx->pages_fetched;
    int failed  = atomic_load(&ctx->pages_failed);
    int skipped = atomic_load(&ctx->pages_skipped);
    
    pthread_mutex_lock(&ctx->queue_lock);
    int queued = ctx->queue.size;
    pthread_mutex_unlock(&ctx->queue_lock);

    int card_w = (form_w - 90) / 4;
    draw_stat_card((Rectangle){ lx, ty, card_w, 180 }, "FETCHED", fetched, C_TEXT_PRI);
    draw_stat_card((Rectangle){ lx + card_w + 30, ty, card_w, 180 }, "FAILED", failed, C_RED);
    draw_stat_card((Rectangle){ lx + (card_w + 30)*2, ty, card_w, 180 }, "SKIPPED", skipped, C_ORANGE);
    draw_stat_card((Rectangle){ lx + (card_w + 30)*3, ty, card_w, 180 }, "QUEUED", queued, C_TEXT_MUT);
    ty += 220;

    // Progress Bar
    draw_text("Progress", lx, ty, 40, C_TEXT_MUT);
    Rectangle prog_bg = { lx + 220, ty + 2, form_w - 450, 44 };
    DrawRectangleRec(prog_bg, C_SURFACE);
    DrawRectangleLinesEx(prog_bg, 1, C_BORDER);
    
    float pct = (float)fetched / ctx->max_pages;
    if (pct > 1) pct = 1;
    DrawRectangle(prog_bg.x + 1, prog_bg.y + 1, (prog_bg.width - 2) * pct, 42, C_ACCENT);

    char prog_str[64];
    sprintf(prog_str, "%d / %d", fetched, ctx->max_pages);
    draw_text(prog_str, lx + form_w - 180, ty, 40, C_TEXT_PRI);
    ty += 80;

    // URL Log Table Header
    int list_h = sh - ty - 60;
    DrawRectangle(lx, ty, form_w, 70, C_SURFACE);
    draw_text("URL", lx + 20, ty + 15, 36, C_TEXT_MUT);
    
    int c2 = lx + form_w - 450;
    int c3 = lx + form_w - 280;
    int c4 = lx + form_w - 140;
    
    draw_text("Status", c2, ty + 15, 36, C_TEXT_MUT);
    draw_text("Links", c3, ty + 15, 36, C_TEXT_MUT);
    draw_text("Outcome", c4, ty + 15, 36, C_TEXT_MUT);
    ty += 70;

    // URL Log Rows
    gs->scroll_offset += GetMouseWheelMove() * 60.0f;
    if (gs->scroll_offset > 0) gs->scroll_offset = 0;

    pthread_mutex_lock(&log->lock);
    int total = log->count;
    int max_scroll = (total * 60) - list_h;
    if (max_scroll < 0) max_scroll = 0;
    if (gs->scroll_offset < -max_scroll) gs->scroll_offset = -max_scroll;

    BeginScissorMode(lx, ty, form_w, list_h);

    for (int i = 0; i < total; i++) {
        int idx = (log->head - 1 - i + GUI_LOG_CAPACITY) % GUI_LOG_CAPACITY;
        gui_log_entry_t *e = &log->entries[idx];

        int row_y = ty + (i * 60) + (int)gs->scroll_offset;
        if (row_y < ty - 60 || row_y > ty + list_h + 60) continue; // cull

        Color c = C_TEXT_MUT;
        if (e->http_status >= 200 && e->http_status < 300) c = C_GREEN;
        else if (e->http_status >= 400) c = C_RED;
        else if (e->http_status == 0) c = C_ORANGE; // skipped

        char trunc_url[80];
        strncpy(trunc_url, e->url, 80);
        trunc_url[65] = '\0';
        if (strlen(e->url) > 65) strcat(trunc_url, "...");

        draw_text(trunc_url, lx + 20, row_y + 12, 32, C_TEXT_PRI);

        char stat_str[32];
        if (e->http_status > 0) sprintf(stat_str, "%ld", e->http_status);
        else strcpy(stat_str, "---");
        draw_text(stat_str, c2, row_y + 12, 32, c);

        char lnk_str[16];
        sprintf(lnk_str, "%d", e->links_found);
        draw_text(lnk_str, c3, row_y + 12, 32, C_TEXT_PRI);

        draw_text(e->outcome, c4, row_y + 12, 32, c);
    }

    EndScissorMode();
    pthread_mutex_unlock(&log->lock);

    return back_clicked;
}
