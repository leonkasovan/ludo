#ifndef LUDO_THREAD_QUEUE_H
#define LUDO_THREAD_QUEUE_H

#include <stddef.h>
#include <time.h>


#ifdef _WIN32
#include <windows.h>
typedef struct {
    CRITICAL_SECTION cs;
} ludo_mutex_t;
typedef CONDITION_VARIABLE ludo_cond_t;
typedef HANDLE ludo_thread_t;
#else
#include <pthread.h>
typedef pthread_mutex_t ludo_mutex_t;
typedef pthread_cond_t  ludo_cond_t;
typedef pthread_t       ludo_thread_t;
#endif

/* A pending URL task pushed from the GUI thread or ludo_module */
typedef struct {
    char url[4096];
    char output_dir[1024]; /* empty = use download manager default */
} URLTask;

/* Thread-safe bounded queue for URLTask items */
typedef struct {
    URLTask     *buf;
    int          capacity;
    int          head;      /* next slot to read  */
    int          tail;      /* next slot to write */
    int          count;
    int          shutdown;
    ludo_mutex_t mutex;
    ludo_cond_t  not_empty;
    ludo_cond_t  not_full;
} TaskQueue;

/* Lifecycle */
int  task_queue_init(TaskQueue *q, int capacity);
void task_queue_destroy(TaskQueue *q);

/* Producer — blocks if full.  push_url copies only the URL (output_dir left empty).
   push_task copies the entire URLTask struct. */
void task_queue_push(TaskQueue *q, const char *url);
void task_queue_push_task(TaskQueue *q, const URLTask *task);

/* Consumer (worker thread) — blocks if empty; returns 0 on shutdown */
int  task_queue_pop(TaskQueue *q, URLTask *out);

/* Signal all waiting consumers to wake up and exit */
void task_queue_shutdown(TaskQueue *q);

/* Cross-platform thread helpers */
int ludo_thread_create(ludo_thread_t *t, void *(*fn)(void *), void *arg);
void ludo_thread_join(ludo_thread_t t);

/* Cross-platform mutex helpers */
void ludo_mutex_init(ludo_mutex_t *m);
void ludo_mutex_destroy(ludo_mutex_t *m);
void ludo_mutex_lock(ludo_mutex_t *m);
void ludo_mutex_unlock(ludo_mutex_t *m);

#endif /* LUDO_THREAD_QUEUE_H */
