#include "fetch.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* libcurl write callback                                               */
/* ------------------------------------------------------------------ */

/*
 * write_callback is called by libcurl each time it receives a chunk of
 * data from the server.  We grow a heap buffer and append each chunk.
 * If the total size would exceed MAX_RESPONSE_BYTES we return 0 to
 * signal an error to libcurl and abort the transfer.
 */
static size_t write_callback(void *incoming_data,
                             size_t item_size,
                             size_t item_count,
                             void  *user_data)
{
    size_t          chunk_bytes = item_size * item_count;
    fetch_result_t *result      = (fetch_result_t *)user_data;

    /* Enforce response size cap */
    if (result->size + chunk_bytes > MAX_RESPONSE_BYTES) {
        fprintf(stderr, "write_callback: response exceeds %d bytes — aborting\n",
                MAX_RESPONSE_BYTES);
        return 0;   /* signals error to libcurl → CURLE_WRITE_ERROR */
    }

    /* Grow the buffer to fit the new chunk plus a NUL terminator */
    char *new_data = realloc(result->data, result->size + chunk_bytes + 1);
    if (new_data == NULL) {
        fprintf(stderr, "write_callback: out of memory\n");
        return 0;
    }

    result->data = new_data;
    memcpy(result->data + result->size, incoming_data, chunk_bytes);
    result->size              += chunk_bytes;
    result->data[result->size] = '\0';   /* keep it null-terminated */

    return chunk_bytes;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

int fetch_page(const char *url, fetch_result_t *result)
{
    result->data        = NULL;
    result->size        = 0;
    result->http_status = 0;
    result->content_type[0] = '\0';

    /* Each thread creates and destroys its own CURL handle */
    CURL *handle = curl_easy_init();
    if (handle == NULL) {
        fprintf(stderr, "fetch_page: curl_easy_init() failed\n");
        return -1;
    }

    /* --- configure the handle --- */
    curl_easy_setopt(handle, CURLOPT_URL,            url);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION,  write_callback);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA,      result);

    /* Follow up to 10 HTTP redirects */
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_MAXREDIRS,      10L);

    /* Reasonable timeout: 20 s total, 10 s to connect */
    curl_easy_setopt(handle, CURLOPT_TIMEOUT,        20L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 10L);

    /* Identify ourselves politely */
    curl_easy_setopt(handle, CURLOPT_USERAGENT,
                     "multithreaded-crawler/1.0 (educational)");

    /* --- perform the request --- */
    CURLcode res = curl_easy_perform(handle);

    /* --- extract response metadata --- */
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &result->http_status);

    /* Content-Type is owned by libcurl; copy it before cleanup */
    char *ct = NULL;
    curl_easy_getinfo(handle, CURLINFO_CONTENT_TYPE, &ct);
    if (ct != NULL) {
        strncpy(result->content_type, ct, sizeof(result->content_type) - 1);
        result->content_type[sizeof(result->content_type) - 1] = '\0';
    }

    curl_easy_cleanup(handle);

    if (res != CURLE_OK) {
        fprintf(stderr, "fetch_page: %s — %s\n",
                url, curl_easy_strerror(res));
        free_fetch_result(result);
        return -1;
    }

    /* Reject non-2xx HTTP responses */
    if (result->http_status < 200 || result->http_status >= 300) {
        fprintf(stderr, "fetch_page: HTTP %ld — %s\n",
                result->http_status, url);
        free_fetch_result(result);
        return -1;
    }

    return 0;
}

void free_fetch_result(fetch_result_t *result)
{
    free(result->data);
    result->data = NULL;
    result->size = 0;
}
