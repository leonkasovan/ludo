#include "thread_queue.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Platform abstraction                                                 */
/* ------------------------------------------------------------------ */

#ifdef _WIN32

/* ---- Windows implementation ---- */

typedef struct {
    void *(*fn)(void *);
    void *arg;
} ludo_thread_start_t;

void ludo_mutex_init(ludo_mutex_t *m) {
    InitializeCriticalSection(&m->cs);
}
void ludo_mutex_destroy(ludo_mutex_t *m) { DeleteCriticalSection(&m->cs); }
void ludo_mutex_lock(ludo_mutex_t *m)    { EnterCriticalSection(&m->cs); }
void ludo_mutex_unlock(ludo_mutex_t *m)  { LeaveCriticalSection(&m->cs); }

static DWORD WINAPI thread_trampoline(LPVOID arg) {
    ludo_thread_start_t *pack = (ludo_thread_start_t *)arg;
    void *(*fn)(void *) = pack->fn;
    void *fnarg = pack->arg;
    free(pack);
    fn(fnarg);
    return 0;
}

int ludo_thread_create(ludo_thread_t *t, void *(*fn)(void *), void *arg) {
    ludo_thread_start_t *pack = (ludo_thread_start_t *)malloc(sizeof(*pack));
    if (!pack) return -1;
    pack->fn = fn;
    pack->arg = arg;
    *t = CreateThread(NULL, 0, thread_trampoline, pack, 0, NULL);
    if (*t == NULL) {
        free(pack);
        return -1;
    }
    return 0;
}

void ludo_thread_join(ludo_thread_t t) {
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
}

/* Use native Windows condition variables so semantics match POSIX. */
static void cond_init(ludo_cond_t *c)    { InitializeConditionVariable(c); }
static void cond_destroy(ludo_cond_t *c) { (void)c; }

static void cond_signal_all(ludo_cond_t *c) { WakeAllConditionVariable(c); }
static void cond_reset(ludo_cond_t *c)      { (void)c; }

static void cond_wait(ludo_cond_t *c, ludo_mutex_t *m) {
    SleepConditionVariableCS(c, &m->cs, INFINITE);
}

#else

/* ---- POSIX implementation ---- */

void ludo_mutex_init(ludo_mutex_t *m)   { pthread_mutex_init(m, NULL); }
void ludo_mutex_destroy(ludo_mutex_t *m){ pthread_mutex_destroy(m); }
void ludo_mutex_lock(ludo_mutex_t *m)   { pthread_mutex_lock(m); }
void ludo_mutex_unlock(ludo_mutex_t *m) { pthread_mutex_unlock(m); }

int ludo_thread_create(ludo_thread_t *t, void *(*fn)(void *), void *arg) {
    return pthread_create(t, NULL, fn, arg);
}
void ludo_thread_join(ludo_thread_t t) { pthread_join(t, NULL); }

static void cond_init(ludo_cond_t *c)         { pthread_cond_init(c, NULL); }
static void cond_destroy(ludo_cond_t *c)      { pthread_cond_destroy(c); }
static void cond_signal_all(ludo_cond_t *c)   { pthread_cond_broadcast(c); }
static void cond_reset(ludo_cond_t *c)        { (void)c; /* no-op */ }
static void cond_wait(ludo_cond_t *c, ludo_mutex_t *m) {
    pthread_cond_wait(c, m);
}

#endif /* _WIN32 */

/* ------------------------------------------------------------------ */
/* TaskQueue                                                            */
/* ------------------------------------------------------------------ */

int task_queue_init(TaskQueue *q, int capacity) {
    q->buf = (URLTask *)malloc((size_t)capacity * sizeof(URLTask));
    if (!q->buf) return -1;
    q->capacity = capacity;
    q->head     = 0;
    q->tail     = 0;
    q->count    = 0;
    q->shutdown = 0;
    ludo_mutex_init(&q->mutex);
    cond_init(&q->not_empty);
    cond_init(&q->not_full);
    return 0;
}

void task_queue_destroy(TaskQueue *q) {
    free(q->buf);
    q->buf = NULL;
    cond_destroy(&q->not_empty);
    cond_destroy(&q->not_full);
    ludo_mutex_destroy(&q->mutex);
}

void task_queue_push(TaskQueue *q, const char *url) {
    URLTask t;
    memset(&t, 0, sizeof(t));
    strncpy(t.url, url, sizeof(t.url) - 1);
    task_queue_push_task(q, &t);
}

void task_queue_push_task(TaskQueue *q, const URLTask *task) {
    ludo_mutex_lock(&q->mutex);
    while (q->count == q->capacity && !q->shutdown) {
        cond_wait(&q->not_full, &q->mutex);
        cond_reset(&q->not_full);
    }
    if (!q->shutdown) {
        q->buf[q->tail] = *task;
        q->tail = (q->tail + 1) % q->capacity;
        q->count++;
        cond_signal_all(&q->not_empty);
    }
    ludo_mutex_unlock(&q->mutex);
}

int task_queue_pop(TaskQueue *q, URLTask *out) {
    ludo_mutex_lock(&q->mutex);
    while (q->count == 0 && !q->shutdown) {
        cond_reset(&q->not_empty);
        cond_wait(&q->not_empty, &q->mutex);
    }
    if (q->shutdown) {
        ludo_mutex_unlock(&q->mutex);
        return 0; /* signal to exit */
    }
    *out    = q->buf[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    cond_signal_all(&q->not_full);
    ludo_mutex_unlock(&q->mutex);
    return 1;
}

void task_queue_shutdown(TaskQueue *q) {
    ludo_mutex_lock(&q->mutex);
    q->shutdown = 1;
    cond_signal_all(&q->not_empty);
    cond_signal_all(&q->not_full);
    ludo_mutex_unlock(&q->mutex);
}
