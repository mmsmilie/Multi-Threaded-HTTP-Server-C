#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <pthread.h>

#include "queue.h"

// Create a pointer to a struct NodeObj
typedef struct NodeObj *Node;

// private NodeObj type
typedef struct NodeObj {
    Node next;
    void *data;
} NodeObj;

// private queue_t type
typedef struct queue {
    Node front;
    Node rear;

    pthread_mutex_t mutex;
    pthread_cond_t full_cond;
    pthread_cond_t empty_cond;

    bool push_wait;
    bool pop_wait;

    int size;
    int max_size;

    bool terminate;

} queue_t;

// Constructors
Node newNode(void *data) {
    Node N = malloc(sizeof(NodeObj));
    assert(N != NULL);
    N->data = data;
    N->next = NULL;
    return N;
}

void freeNode(Node *pN) {
    if (pN != NULL && *pN != NULL) {
        free(*pN);
        *pN = NULL;
    }
}

queue_t *queue_new(int size) {
    queue_t *Q = malloc(sizeof(queue_t));
    assert(Q != NULL);
    Q->front = NULL;
    Q->rear = NULL;

    int res;
    res = pthread_mutex_init(&Q->mutex, NULL);
    assert(res == 0);
    res = pthread_cond_init(&Q->full_cond, NULL);
    assert(res == 0);
    res = pthread_cond_init(&Q->empty_cond, NULL);
    assert(res == 0);

    Q->push_wait = false;
    Q->pop_wait = false;

    Q->size = 0;
    Q->max_size = size;

    Q->terminate = false;

    return Q;
}

// Destructor
void queue_delete(queue_t **q) {
    if (q != NULL && *q != NULL) {
        queue_t *Q = *q;
        Node N = Q->front;
        while (N != NULL) {
            Node next = N->next;
            freeNode(&N);
            N = next;
        }

        pthread_mutex_destroy(&Q->mutex);
        pthread_cond_destroy(&Q->full_cond);
        pthread_cond_destroy(&Q->empty_cond);

        free(Q);
        *q = NULL;
    }
}

// Access funcitons
int queue_size(queue_t *q) {
    return q->size;
}

bool queue_empty(queue_t *q) {
    return q->size == 0;
}

bool queue_full(queue_t *q) {
    return q->size == q->max_size;
}

// Queue operations
bool queue_push(queue_t *q, void *elem) {

    if (q == NULL) {
        return false;
    }

    pthread_mutex_lock(&q->mutex);

    while (queue_full(q)) {
        pthread_cond_wait(&q->full_cond, &q->mutex);
    }

    Node new = newNode(elem);

    if (q->rear == NULL) {
        q->front = q->rear = new;
    } else {
        q->rear->next = new;
        q->rear = new;
    }

    q->size++;

    pthread_cond_signal(&q->empty_cond);

    pthread_mutex_unlock(&q->mutex);

    return true;
}

bool queue_pop(queue_t *q, void **elem) {

    if (q == NULL) {
        return false;
    }

    pthread_mutex_lock(&q->mutex);

    while (queue_empty(q)) {
        pthread_cond_wait(&q->empty_cond, &q->mutex);
    }

    Node old = q->front;

    *elem = old->data;

    q->front = q->front->next;

    if (q->front == NULL) {
        q->rear = NULL;
    }

    freeNode(&old);

    q->size--;

    pthread_cond_signal(&q->full_cond);
    pthread_mutex_unlock(&q->mutex);

    return true;
}

void queue_terminate(queue_t *q) {
    q->terminate = true;
}

bool queue_terminated(queue_t *q) {
    return q->terminate;
}
