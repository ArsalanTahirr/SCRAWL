#include "parse.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

#define INITIAL_CAPACITY 16

static int list_append(link_list_t *links, const char *url)
{
    if (links->count == links->capacity) {
        size_t new_cap  = links->capacity * 2;
        char **new_urls = realloc(links->urls, new_cap * sizeof(char *));
        if (new_urls == NULL) {
            perror("list_append: realloc");
            return -1;
        }
        links->urls     = new_urls;
        links->capacity = new_cap;
    }

    links->urls[links->count] = strdup(url);
    if (links->urls[links->count] == NULL) {
        perror("list_append: strdup");
        return -1;
    }
    links->count++;
    return 0;
}

/* ------------------------------------------------------------------ */
/* URL utility — extract host                                           */
/* ------------------------------------------------------------------ */

char *url_extract_host(const char *url, char *buf, size_t buf_size)
{
    const char *after_scheme = strstr(url, "://");
    if (after_scheme == NULL) return NULL;
    after_scheme += 3;

    /* Host ends at the first '/', '?', '#', or end-of-string */
    size_t len = strcspn(after_scheme, "/?#");
    if (len == 0 || len >= buf_size) return NULL;

    /* Lowercase the host */
    for (size_t i = 0; i < len; i++) {
        buf[i] = (char)tolower((unsigned char)after_scheme[i]);
    }
    buf[len] = '\0';
    return buf;
}

/* ------------------------------------------------------------------ */
/* URL normalization                                                     */
/* ------------------------------------------------------------------ */

/*
 * normalize_url – modify `url` in-place:
 *   - lowercase scheme (http/https)
 *   - lowercase host
 *   - strip fragment (#...)
 *   - strip a trailing '?' with no query string
 *
 * url must be a writable heap buffer.
 */
static void normalize_url(char *url)
{
    /* Lowercase up to and including "://" and then the host */
    char *p = url;

    /* Lowercase scheme */
    while (*p && *p != ':') {
        *p = (char)tolower((unsigned char)*p);
        p++;
    }
    if (p[0] == ':' && p[1] == '/' && p[2] == '/') {
        p += 3;   /* skip "://" */
        /* Lowercase host (up to first '/', '?', '#') */
        while (*p && *p != '/' && *p != '?' && *p != '#') {
            *p = (char)tolower((unsigned char)*p);
            p++;
        }
    }

    /* Strip fragment */
    char *frag = strchr(url, '#');
    if (frag != NULL) {
        *frag = '\0';
    }

    /* Strip trailing '?' with no query */
    size_t len = strlen(url);
    if (len > 0 && url[len - 1] == '?') {
        url[len - 1] = '\0';
    }
}

/* ------------------------------------------------------------------ */
/* Absolute URL resolution                                              */
/* ------------------------------------------------------------------ */

/*
 * build_absolute_url – given a base URL and a (possibly relative) href,
 * return a newly allocated, normalized absolute URL string, or NULL on
 * error / skip.
 *
 * Handles:
 *   1. Already-absolute  http:// or https://
 *   2. Protocol-relative //host/path
 *   3. Root-relative     /path
 *   4. Path-relative     ../path, ./path, bare file.html  ← NEW
 *
 * Skips: fragment-only (#...), javascript:, mailto:, data:
 */
static char *build_absolute_url(const char *base_url, const char *href)
{
    /* Skip empties and special schemes */
    if (href == NULL || href[0] == '\0')          return NULL;
    if (href[0] == '#')                            return NULL;
    if (strncmp(href, "javascript:", 11) == 0)    return NULL;
    if (strncmp(href, "mailto:",      7) == 0)    return NULL;
    if (strncmp(href, "data:",        5) == 0)    return NULL;
    if (strncmp(href, "tel:",         4) == 0)    return NULL;

    /* Case 1: already absolute */
    if (strncmp(href, "http://",  7) == 0 ||
        strncmp(href, "https://", 8) == 0) {
        char *result = strdup(href);
        if (result) normalize_url(result);
        return result;
    }

    /* Determine base scheme */
    char scheme[8] = "http";
    if (strncmp(base_url, "https://", 8) == 0) {
        strcpy(scheme, "https");
    }

    /* Case 2: protocol-relative  "//host/path" */
    if (href[0] == '/' && href[1] == '/') {
        size_t needed = strlen(scheme) + 1 + strlen(href) + 1;
        char  *result = malloc(needed);
        if (result == NULL) return NULL;
        snprintf(result, needed, "%s:%s", scheme, href);
        normalize_url(result);
        return result;
    }

    /* Extract scheme://host from base_url */
    const char *after_scheme = strstr(base_url, "://");
    if (after_scheme == NULL) return NULL;
    after_scheme += 3;

    const char *path_start = strchr(after_scheme, '/');
    size_t host_len = (path_start != NULL)
                        ? (size_t)(path_start - after_scheme)
                        : strlen(after_scheme);

    /* Case 3: root-relative  "/path" */
    if (href[0] == '/') {
        size_t needed = strlen(scheme) + 3 + host_len + strlen(href) + 1;
        char  *result = malloc(needed);
        if (result == NULL) return NULL;
        snprintf(result, needed, "%s://%.*s%s",
                 scheme, (int)host_len, after_scheme, href);
        normalize_url(result);
        return result;
    }

    /*
     * Case 4: path-relative  ("../foo", "./foo", "foo.html", etc.)
     *
     * Algorithm:
     *   a) Take the base URL's path (everything after host, up to '?' or '#')
     *   b) Strip the last path segment (filename) to get the directory
     *   c) Append href
     *   d) Resolve ".." and "." segments
     */

    /* Build base directory path */
    const char *base_path = (path_start != NULL) ? path_start : "/";

    /* Find end of path (before query/fragment) */
    const char *path_end = base_path + strcspn(base_path, "?#");

    /* Walk back to last '/' to get the directory */
    const char *last_slash = base_path;
    for (const char *q = base_path; q < path_end; q++) {
        if (*q == '/') last_slash = q;
    }
    size_t dir_len = (size_t)(last_slash - base_path) + 1; /* include '/' */

    /* Combine: scheme://host + dir + href */
    size_t needed = strlen(scheme) + 3 + host_len + dir_len + strlen(href) + 1;
    char  *combined = malloc(needed);
    if (combined == NULL) return NULL;
    int written = snprintf(combined, needed, "%s://%.*s%.*s%s",
                           scheme,
                           (int)host_len, after_scheme,
                           (int)dir_len, base_path,
                           href);
    if (written < 0 || (size_t)written >= needed) {
        free(combined);
        return NULL;
    }

    /*
     * Resolve ".." and "." in the path portion of `combined`.
     * We work on a copy of just the path to simplify pointer arithmetic.
     */
    char *path_ptr = combined + strlen(scheme) + 3 + host_len;

    /* Iteratively resolve segments */
    char *out = malloc(strlen(path_ptr) + 1);
    if (out == NULL) { free(combined); return NULL; }

    char *w = out;          /* write pointer into `out` */
    const char *r = path_ptr;

    while (*r != '\0' && *r != '?' && *r != '#') {
        if (r[0] == '/' && r[1] == '.' && r[2] == '.' &&
            (r[3] == '/' || r[3] == '\0' || r[3] == '?' || r[3] == '#')) {
            /* Go up one directory */
            if (w > out + 1) {   /* don't go above root */
                w--;             /* step back over previous '/' */
                while (w > out && *(w-1) != '/') w--;
            }
            r += 3;
            if (*r == '/') r++;
        } else if (r[0] == '/' && r[1] == '.' &&
                   (r[2] == '/' || r[2] == '\0' || r[2] == '?' || r[2] == '#')) {
            /* Current dir — skip */
            r += 2;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';

    /* Reconstruct: scheme://host + resolved_path + remaining (query/frag) */
    size_t prefix_len = strlen(scheme) + 3 + host_len;
    size_t result_len = prefix_len + strlen(out) + strlen(r) + 1;
    char  *result     = malloc(result_len);
    if (result == NULL) { free(out); free(combined); return NULL; }

    snprintf(result, result_len, "%s://%.*s%s%s",
             scheme, (int)host_len, after_scheme, out, r);

    free(out);
    free(combined);

    normalize_url(result);
    return result;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void init_link_list(link_list_t *links)
{
    links->count    = 0;
    links->capacity = INITIAL_CAPACITY;
    links->urls     = malloc(INITIAL_CAPACITY * sizeof(char *));
    /* if malloc fails, capacity stays set but urls is NULL;
       list_append will handle the NULL gracefully via realloc */
}

void free_link_list(link_list_t *links)
{
    for (size_t i = 0; i < links->count; i++) {
        free(links->urls[i]);
    }
    free(links->urls);
    links->urls     = NULL;
    links->count    = 0;
    links->capacity = 0;
}

/*
 * extract_links – scan HTML text for href="..." and href='...' patterns.
 *
 * If `allowed_host` is non-NULL, only URLs whose host matches are kept
 * (domain-scoping).  Pass NULL to allow all hosts.
 */
int extract_links(const char *html,
                  const char *base_url,
                  const char *allowed_host,
                  link_list_t *links)
{
    if (html == NULL || base_url == NULL) return 0;

    const char *cursor = html;

    while ((cursor = strstr(cursor, "href")) != NULL) {
        cursor += 4;   /* skip "href" */

        /* Skip whitespace then expect '=' */
        while (*cursor == ' ' || *cursor == '\t') cursor++;
        if (*cursor != '=') continue;
        cursor++;   /* skip '=' */

        /* Skip whitespace */
        while (*cursor == ' ' || *cursor == '\t') cursor++;

        /* Determine quote character (' or ") */
        char quote = *cursor;
        if (quote != '"' && quote != '\'') continue;
        cursor++;   /* skip opening quote */

        /* Find closing quote */
        const char *end = strchr(cursor, quote);
        if (end == NULL) continue;

        size_t href_len = (size_t)(end - cursor);
        if (href_len == 0 || href_len >= 2048) {
            cursor = end + 1;
            continue;
        }

        /* Copy the raw href value */
        char href[2048];
        memcpy(href, cursor, href_len);
        href[href_len] = '\0';

        cursor = end + 1;   /* advance past closing quote */

        /* Resolve to a normalized absolute URL */
        char *absolute = build_absolute_url(base_url, href);
        if (absolute == NULL) continue;

        /* Domain-scope filter */
        if (allowed_host != NULL) {
            char link_host[256];
            if (url_extract_host(absolute, link_host, sizeof(link_host)) == NULL
                || strcmp(link_host, allowed_host) != 0) {
                free(absolute);
                continue;
            }
        }

        if (list_append(links, absolute) != 0) {
            free(absolute);
            return -1;   /* OOM */
        }
        free(absolute);
    }

    return 0;
}
