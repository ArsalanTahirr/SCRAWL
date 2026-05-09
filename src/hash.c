#include "hash.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

/*
 * djb2 – classic Dan Bernstein string hash.
 * Fast, simple, and good enough for URL strings.
 */
static unsigned long djb2(const char *str)
{
    unsigned long hash = 5381;
    int c;

    while ((c = (unsigned char)*str++) != '\0') {
        /* hash = hash * 33 + c */
        hash = ((hash << 5) + hash) + (unsigned long)c;
    }
    return hash;
}

static size_t bucket_index(const char *url)
{
    return (size_t)(djb2(url) & (HASH_TABLE_SIZE - 1));
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                            */
/* ------------------------------------------------------------------ */

void init_hash_table(hash_table_t *ht)
{
    memset(ht->buckets, 0, sizeof(ht->buckets));
    ht->count = 0;
}

void destroy_hash_table(hash_table_t *ht)
{
    for (size_t i = 0; i < HASH_TABLE_SIZE; i++) {
        hash_entry_t *entry = ht->buckets[i];
        while (entry != NULL) {
            hash_entry_t *next = entry->next;
            free(entry->url);
            free(entry);
            entry = next;
        }
        ht->buckets[i] = NULL;
    }
    ht->count = 0;
}

/* ------------------------------------------------------------------ */
/* Operations                                                           */
/* ------------------------------------------------------------------ */

int hash_contains(const hash_table_t *ht, const char *url)
{
    size_t        idx   = bucket_index(url);
    hash_entry_t *entry = ht->buckets[idx];

    while (entry != NULL) {
        if (strcmp(entry->url, url) == 0) {
            return 1;   /* found */
        }
        entry = entry->next;
    }
    return 0;   /* not found */
}

int hash_insert(hash_table_t *ht, const char *url)
{
    size_t idx = bucket_index(url);

    /* Check whether the URL already exists */
    hash_entry_t *entry = ht->buckets[idx];
    while (entry != NULL) {
        if (strcmp(entry->url, url) == 0) {
            return 0;   /* already present */
        }
        entry = entry->next;
    }

    /* Allocate a new entry and prepend it to the bucket chain */
    hash_entry_t *new_entry = malloc(sizeof(hash_entry_t));
    if (new_entry == NULL) {
        perror("hash_insert: malloc");
        return -1;
    }

    new_entry->url = strdup(url);
    if (new_entry->url == NULL) {
        perror("hash_insert: strdup");
        free(new_entry);
        return -1;
    }

    new_entry->next    = ht->buckets[idx];
    ht->buckets[idx]   = new_entry;
    ht->count++;
    return 1;   /* newly inserted */
}
