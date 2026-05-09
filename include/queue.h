#ifndef QUEUE_H
#define QUEUE_H

#include <stddef.h>

/* A single node in the linked-list queue */
typedef struct queue_node {
    char              *url;
    struct queue_node *next;
} queue_node_t;

/* FIFO queue backed by a singly-linked list */
typedef struct {
    queue_node_t *head;   /* dequeue from here */
    queue_node_t *tail;   /* enqueue here      */
    size_t        size;
} url_queue_t;

/* Lifecycle */
void  init_queue(url_queue_t *q);
void  destroy_queue(url_queue_t *q);

/* Operations (caller must hold the external mutex) */
int   enqueue(url_queue_t *q, const char *url);   /* 0 on success, -1 on OOM */
char *dequeue(url_queue_t *q);                     /* caller must free(); NULL if empty */
int   queue_is_empty(const url_queue_t *q);

#endif /* QUEUE_H */
