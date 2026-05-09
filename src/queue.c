#include "queue.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Lifecycle                                                            */
/* ------------------------------------------------------------------ */

void init_queue(url_queue_t *q)
{
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
}

void destroy_queue(url_queue_t *q)
{
    /* Drain any URLs that were never consumed */
    char *url;
    while ((url = dequeue(q)) != NULL) {
        free(url);
    }
}

/* ------------------------------------------------------------------ */
/* Operations                                                           */
/* ------------------------------------------------------------------ */

/*
 * enqueue – append a copy of `url` to the tail of the queue.
 * Returns 0 on success, -1 if memory allocation fails.
 */
int enqueue(url_queue_t *q, const char *url)
{
    queue_node_t *node = malloc(sizeof(queue_node_t));
    if (node == NULL) {
        perror("enqueue: malloc node");
        return -1;
    }

    node->url  = strdup(url);
    node->next = NULL;

    if (node->url == NULL) {
        perror("enqueue: strdup");
        free(node);
        return -1;
    }

    if (q->tail == NULL) {
        /* Queue was empty */
        q->head = node;
        q->tail = node;
    } else {
        q->tail->next = node;
        q->tail       = node;
    }

    q->size++;
    return 0;
}

/*
 * dequeue – remove and return the URL at the head of the queue.
 * Returns NULL if the queue is empty.
 * The caller is responsible for free()-ing the returned string.
 */
char *dequeue(url_queue_t *q)
{
    if (q->head == NULL) {
        return NULL;
    }

    queue_node_t *node = q->head;
    char         *url  = node->url;   /* transfer ownership to caller */

    q->head = node->next;
    if (q->head == NULL) {
        q->tail = NULL;               /* queue is now empty */
    }

    free(node);
    q->size--;
    return url;
}

int queue_is_empty(const url_queue_t *q)
{
    return q->head == NULL;
}
