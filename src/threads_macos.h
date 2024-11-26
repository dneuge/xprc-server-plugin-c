#ifndef THREADS_MACOS_H
#define THREADS_MACOS_H

#include <pthread.h>

typedef pthread_mutex_t mtx_t;
typedef pthread_t thrd_t;

typedef int (*thrd_start_t)(void*);

#define thrd_success (0)
#define thrd_error (1)

#define mtx_plain (1)
#define mtx_recursive (3)

int mtx_init(mtx_t *mutex, int type);
void mtx_destroy(mtx_t *mutex);
int mtx_lock(mtx_t *mutex);
int mtx_unlock(mtx_t *mutex);

int thrd_create(thrd_t *thr, thrd_start_t func, void *arg);
int thrd_sleep(const struct timespec *duration, struct timespec *remaining);
int thrd_join(thrd_t thr, int *res);


#endif
