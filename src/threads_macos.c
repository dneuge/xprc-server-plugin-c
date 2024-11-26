#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"

#include "threads_macos.h"

int mtx_init(mtx_t *mutex, int type) {
    int err = 0;
    int fin_err = 0;

    if (type != mtx_plain && type != (mtx_plain | mtx_recursive)) {
        printf("[threads_macos] mtx_init unsupported type requested: %d\n", type);
        return thrd_error;
    }

    pthread_mutexattr_t attr = {0};
    err = pthread_mutexattr_init(&attr);
    if (err) {
        printf("[threads_macos] pthread_mutexattr_init error: %d %s\n", err, strerror(err));
        return thrd_error;
    }
    
    if (type & mtx_recursive) {
        err = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        if (err) {
            printf("[threads_macos] pthread_mutexattr_settype PTHREAD_MUTEX_RECURSIVE error: %d %s\n", err, strerror(err));
            goto end;
        }
    }
    
    err = pthread_mutex_init(mutex, &attr);
    if (err) {
        printf("[threads_macos] pthread_mutex_init error: %d %s\n", err, strerror(err));
    }

end:
    fin_err = pthread_mutexattr_destroy(&attr);
    if (fin_err) {
        printf("[threads_macos] pthread_mutexattr_destroy error: %d %s\n", fin_err, strerror(fin_err));
    }
    
    return err ? thrd_error : thrd_success;
}

void mtx_destroy(mtx_t *mutex) {
    int err = pthread_mutex_destroy(mutex);
    if (err) {
        printf("[threads_macos] pthread_mutex_destroy error: %d %s\n", err, strerror(err));
    }
}

int mtx_lock(mtx_t *mutex) {
    int err = pthread_mutex_lock(mutex);
    if (err) {
        printf("[threads_macos] pthread_mutex_lock error: %d %s\n", err, strerror(err));
    }

    return err ? thrd_error : thrd_success;
}

int mtx_unlock(mtx_t *mutex) {
    int err = pthread_mutex_unlock(mutex);
    if (err) {
        printf("[threads_macos] pthread_mutex_unlock error: %d %s\n", err, strerror(err));
    }

    return err ? thrd_error : thrd_success;
}

typedef struct {
    thrd_start_t actual_func;
    void *actual_arg;
    int res;
} wrapped_thread_t;

static void* wrap_thread_func(void *arg) {
    wrapped_thread_t *wrapped = arg;
    wrapped->res = wrapped->actual_func(wrapped->actual_arg);
    return arg;
}

int thrd_create(thrd_t *thr, thrd_start_t func, void *arg) {
    wrapped_thread_t *wrapped = zmalloc(sizeof(wrapped_thread_t));
    if (!wrapped) {
        printf("[threads_macos] thrd_create out of memory?\n");
        return thrd_error;
    }

    wrapped->actual_func = func;
    wrapped->actual_arg = arg;

    int err = pthread_create(thr, NULL, wrap_thread_func, wrapped);
    if (err) {
        printf("[threads_macos] pthread_create error: %d %s\n", err, strerror(err));
        free(wrapped);
    }

    return err ? thrd_error : thrd_success;
}

int thrd_sleep(const struct timespec *duration, struct timespec *remaining) {
    return nanosleep(duration, remaining);
}

int thrd_join(thrd_t thr, int *res) {
    wrapped_thread_t *wrapped = NULL;
    int err = pthread_join(thr, (void**) &wrapped);
    if (err) {
        printf("[threads_macos] pthread_join error: %d %s\n", err, strerror(err));
        return thrd_error;
    }

    if (!wrapped) {
        printf("[threads_macos] joined thread returned NULL (not our wrapper?!)\n");

        if (res) {
            *res = 0;
        }
    } else {
        if (res) {
            *res = wrapped->res;
        }

        free(wrapped);
    }

    return thrd_success;
}
