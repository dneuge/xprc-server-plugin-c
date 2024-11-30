#ifndef THREADS_MACOS_H
#define THREADS_MACOS_H

#include <pthread.h>

typedef pthread_mutex_t mtx_t;
typedef pthread_t thrd_t;
typedef pthread_cond_t cnd_t;

typedef int (*thrd_start_t)(void*);

#define thrd_success (0)
#define thrd_error (1)
#define thrd_nomem (2)
#define thrd_timedout (3)

#define mtx_plain (1 << 0)
#define mtx_recursive (1 << 1)

int mtx_init(mtx_t *mutex, int type);
void mtx_destroy(mtx_t *mutex);
int mtx_lock(mtx_t *mutex);
int mtx_unlock(mtx_t *mutex);

int thrd_create(thrd_t *thr, thrd_start_t func, void *arg);
int thrd_sleep(const struct timespec *duration, struct timespec *remaining);
int thrd_join(thrd_t thr, int *res);
void thrd_yield();

int cnd_init(cnd_t *cond);
void cnd_destroy(cnd_t *cond);
int cnd_wait(cnd_t *cond, mtx_t *mutex);
int cnd_broadcast(cnd_t *cond);
int cnd_timedwait(cnd_t *cond, mtx_t *mutex, const struct timespec *time_point);

#endif
