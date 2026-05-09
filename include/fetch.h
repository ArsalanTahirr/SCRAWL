#ifndef FETCH_H
#define FETCH_H

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Configuration                                                        */
/* ------------------------------------------------------------------ */

/* Maximum response body size we will accept (10 MiB).
 * write_callback aborts the transfer if this limit is exceeded. */
#define MAX_RESPONSE_BYTES (10 * 1024 * 1024)

/* ------------------------------------------------------------------ */
/* Result buffer                                                        */
/* ------------------------------------------------------------------ */

/*
 * fetch_result_t holds the raw HTTP response body and metadata.
 * Memory is owned by the caller; call free_fetch_result() when done.
 */
typedef struct {
    char  *data;              /* null-terminated response body          */
    size_t size;              /* number of bytes (excl. NUL)            */
    long   http_status;       /* HTTP response code (e.g. 200, 404)    */
    char   content_type[128]; /* Content-Type header value (may be "")  */
} fetch_result_t;

/* ------------------------------------------------------------------ */
/* API                                                                  */
/* ------------------------------------------------------------------ */

/*
 * fetch_page – download the content at `url` into `result`.
 *
 * Each calling thread creates and destroys its own CURL handle so
 * that no handle is ever shared between threads.
 *
 * Returns  0  on success (HTTP 2xx, content within size limit).
 * Returns -1  on network/curl error, non-2xx HTTP status, or size
 *             limit exceeded.  result->http_status is always populated
 *             when an HTTP response was received (even on -1 return).
 *
 * On success result->data must be freed by the caller via
 * free_fetch_result().
 */
int  fetch_page(const char *url, fetch_result_t *result);

void free_fetch_result(fetch_result_t *result);

#endif /* FETCH_H */
