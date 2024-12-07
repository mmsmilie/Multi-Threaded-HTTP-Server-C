#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <semaphore.h>

#include "rwlock.h"

// Comment for change so I can push to correct spelling mistake

typedef enum { reader, writer } operation;

// Data Structure
typedef struct rwlock {
    pthread_mutex_t lock;
    pthread_cond_t readers_ok;
    pthread_cond_t writers_ok;

    sem_t sem;

    int active_readers;
    int active_writers;
    int waiting_readers;
    int waiting_writers;

    PRIORITY priority;

    int consecutive_reads;
    int consecutive_writes;

    int n_way;
} rwlock_t;

rwlock_t *rwlock_new(PRIORITY p, uint32_t n) {

    // Allocate memory for rwlock_t
    rwlock_t *rw = malloc(sizeof(rwlock_t));
    if (rw == NULL) {
        fprintf(stderr, "Failed to allocate memory for rwlock\n");
        exit(1);
    }

    // Initialize pthread components
    pthread_mutex_init(&rw->lock, NULL);
    pthread_cond_init(&rw->readers_ok, NULL);
    pthread_cond_init(&rw->writers_ok, NULL);

    // Initialize semaphores
    sem_init(&rw->sem, 0, n);

    // Set int variables
    rw->active_readers = 0;
    rw->active_writers = 0;
    rw->waiting_readers = 0;
    rw->waiting_writers = 0;

    // Set priority
    rw->priority = p;

    // Set n_way
    rw->n_way = n;
    rw->consecutive_reads = 0;
    rw->consecutive_writes = 0;

    return rw;
}

void rwlock_delete(rwlock_t **rw) {

    // Delete pthread components
    pthread_mutex_destroy(&(*rw)->lock);
    pthread_cond_destroy(&(*rw)->readers_ok);
    pthread_cond_destroy(&(*rw)->writers_ok);

    // Delete semaphores
    sem_destroy(&(*rw)->sem);

    // Free memory
    free(*rw);

    // Make pointer NULL
    *rw = NULL;

    return;
}

// N-Way Functions
void update_access(rwlock_t *rw, operation op) {
    if (op == reader) {
        rw->consecutive_reads++;
        rw->consecutive_writes = 0;
    } else if (op == writer) {
        rw->consecutive_writes++;
        rw->consecutive_reads = 0;
    }
}

int priority_handle(rwlock_t *rw, operation op) {
    bool switch_op
        = false; // Flag to indicate if we should switch operation type due to N_Way policy

    if (rw->priority == N_WAY) {
        if ((op == reader && rw->consecutive_reads >= rw->n_way)
            || (op == writer && rw->consecutive_writes >= 1)) {
            switch_op = true; // Time to switch operation type
        }
    }

    if (op == reader) {
        // Readers wait if there's an active writer, or if N_Way switch condition is met and writers are waiting
        return rw->active_writers > 0 || (switch_op && rw->waiting_writers > 0);
    } else { // writer
        // Writers wait if there's an active reader or writer, or if N_Way switch condition is met and readers are waiting
        return rw->active_readers > 0 || rw->active_writers > 0
               || (switch_op && rw->waiting_readers > 0);
    }
}

// Reader Functions
void reader_lock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);
    rw->waiting_readers++;
    while (priority_handle(rw, reader)) {
        pthread_cond_wait(&rw->readers_ok, &rw->lock);
    }
    rw->active_readers++;
    rw->waiting_readers--;
    update_access(rw, reader);
    pthread_mutex_unlock(&rw->lock);
}

void reader_unlock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);
    rw->active_readers--;
    if (rw->active_readers == 0) {
        if (rw->priority == N_WAY && rw->consecutive_reads >= rw->n_way
            && rw->waiting_writers > 0) {
            pthread_cond_signal(&rw->writers_ok); // Prioritize writer after N_Way threshold
        } else if (rw->waiting_writers > 0) {
            pthread_cond_signal(&rw->writers_ok); // Normal case, allow writer if any are waiting
        } else {
            pthread_cond_broadcast(&rw->readers_ok); // Otherwise, let all waiting readers proceed
        }
    }
    pthread_mutex_unlock(&rw->lock);
}

// Writing Functions

void writer_lock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);
    rw->waiting_writers++;
    while (priority_handle(rw, writer)) {
        pthread_cond_wait(&rw->writers_ok, &rw->lock);
    }
    rw->active_writers++;
    rw->waiting_writers--;
    update_access(rw, writer);
    pthread_mutex_unlock(&rw->lock);
}

void writer_unlock(rwlock_t *rw) {
    pthread_mutex_lock(&rw->lock);
    rw->active_writers--;

    if (rw->priority == N_WAY && rw->consecutive_writes >= rw->n_way && rw->waiting_readers > 0) {
        pthread_cond_broadcast(&rw->readers_ok); // Prioritize readers after N_Way threshold
        rw->consecutive_writes = 0; // Reset counter to allow for operation switch
    } else if (rw->waiting_readers > 0) {
        pthread_cond_broadcast(&rw->readers_ok); // If readers are waiting, let them proceed
    } else if (rw->waiting_writers > 0) {
        pthread_cond_signal(
            &rw->writers_ok); // If no readers are waiting, but writers are, allow next writer
    }
    pthread_mutex_unlock(&rw->lock);
}
