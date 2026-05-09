#ifndef HASH_H
#define HASH_H

#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Configuration                                                        */
/* ------------------------------------------------------------------ */

#define HASH_TABLE_SIZE 65536  /* must be a power of two */

/* ------------------------------------------------------------------ */
/* Types                                                                */
/* ------------------------------------------------------------------ */

/* One entry in a hash-table bucket chain */
typedef struct hash_entry {
    char              *url;
    struct hash_entry *next;
} hash_entry_t;

/* The hash table itself */
typedef struct {
    hash_entry_t *buckets[HASH_TABLE_SIZE];
    size_t        count;
} hash_table_t;

/* ------------------------------------------------------------------ */
/* API                                                                  */
/* ------------------------------------------------------------------ */

void init_hash_table(hash_table_t *ht);
void destroy_hash_table(hash_table_t *ht);

/*
 * hash_insert – add url to the table.
 * Returns 1 if the url was newly inserted, 0 if it was already present,
 * and -1 on memory-allocation failure.
 */
int  hash_insert(hash_table_t *ht, const char *url);

/*
 * hash_contains – return 1 if the url is in the table, 0 otherwise.
 */
int  hash_contains(const hash_table_t *ht, const char *url);

#endif /* HASH_H */
