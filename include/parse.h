#ifndef PARSE_H
#define PARSE_H

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Link list                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    char  **urls;       /* heap-allocated array of heap-allocated strings */
    size_t  count;
    size_t  capacity;
} link_list_t;

void init_link_list(link_list_t *links);
void free_link_list(link_list_t *links);

/* ------------------------------------------------------------------ */
/* URL utilities (also used by robots.c)                               */
/* ------------------------------------------------------------------ */

/*
 * url_extract_host – copy the host (and port, if present) from `url`
 * into `buf` (max `buf_size` bytes including NUL).
 * Returns buf on success, NULL on failure.
 */
char *url_extract_host(const char *url, char *buf, size_t buf_size);

/* ------------------------------------------------------------------ */
/* Link extraction                                                      */
/* ------------------------------------------------------------------ */

/*
 * extract_links – scan `html` for href="..." patterns and populate
 * `links` with absolute URLs resolved against `base_url`.
 *
 * If `allowed_host` is non-NULL only URLs whose host matches
 * `allowed_host` are added (domain-scoping).
 *
 * Returns 0 on success, -1 on OOM.
 */
int extract_links(const char *html,
                  const char *base_url,
                  const char *allowed_host,   /* NULL = no restriction */
                  link_list_t *links);

#endif /* PARSE_H */
