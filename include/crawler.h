#ifndef CRAWLER_H
#define CRAWLER_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

#include "queue.h"
#include "hash.h"
#include "robots.h"

/* ------------------------------------------------------------------ */
/* Compile-time defaults (overridable at runtime via CLI)               */
/* ------------------------------------------------------------------ */

#define DEFAULT_NUM_THREADS  4
#define DEFAULT_MAX_PAGES    1
#define CRAWLER_USER_AGENT   "multithreaded-crawler/1.0 (educational)"

/* ------------------------------------------------------------------ */
/* Shared crawler context                                               */
/*                                                                      *
 * One instance is created in main() and a pointer is passed to every  *
 * worker thread.  All inter-thread communication goes through here.    *
 * ------------------------------------------------------------------ */
typedef struct crawler_context {
    /* ---- Work queue ---- */
    url_queue_t      queue;
    pthread_mutex_t  queue_lock;
    pthread_cond_t   queue_cond;

    /* ---- Visited-URL registry ---- */
    hash_table_t     visited;
    pthread_rwlock_t visited_rwlock;

    /* ---- robots.txt cache (host → robots_rules_t*) ---- */
    /* Simple open-addressed cache: key = host string, value = rules ptr.
     * NULL value means "not yet fetched"; we use a sentinel to mean
     * "fetched but allow-all".  Protected by robots_cache_lock. */
    pthread_mutex_t  robots_cache_lock;
    hash_table_t     robots_hosts;   /* just tracks which hosts we have fetched */
    /* Actual rules stored as parallel arrays would be complex; instead we
     * store them inline in a linked list keyed by host. */
    struct robots_cache_entry {
        char                       *host;
        robots_rules_t             *rules;   /* NULL = allow-all */
        struct robots_cache_entry  *next;
    } *robots_cache;

    /* ---- GUI support ---- */
    struct gui_log *gui_log;   /* Forward declared; NULL in CLI mode */

    /* ---- Runtime configuration ---- */
    int          num_threads;     /* -t flag                */
    int          max_pages;       /* -n flag                */
    int          domain_scope;    /* -d flag: 1 = same-host only */
    int          verbose;         /* -v flag                */
    char        *seed_host;       /* extracted from seed URL */
    FILE        *output_file;     /* -o flag: JSONL output  */
    pthread_mutex_t output_lock;  /* serialise JSONL writes */

    /* ---- Counters ---- */
    atomic_int   active_threads;  /* threads currently doing work */
    int          shutdown_flag;   /* set to 1 to signal exit      */
    int          pages_fetched;   /* protected by queue_lock       */
    atomic_int   pages_failed;    /* fetch errors                  */
    atomic_int   pages_skipped;   /* non-HTML or robots-blocked    */

} crawler_context_t;

/* ------------------------------------------------------------------ */
/* API                                                                  */
/* ------------------------------------------------------------------ */

void  init_crawler_context(crawler_context_t *ctx);
void  destroy_crawler_context(crawler_context_t *ctx);

/* worker_thread is passed to pthread_create(); arg = crawler_context_t* */
void *worker_thread(void *arg);

#endif /* CRAWLER_H */
