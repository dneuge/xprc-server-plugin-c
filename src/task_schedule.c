#include <stdlib.h>
#include <string.h>

#include "task_schedule.h"

#include "logger.h"
#include "utils.h"

/**
 * Maximum time (in milliseconds) to wait for gaining a schedule lock to run post-processing tasks.
 * The post-processing thread interlocks with X-Plane's main thread, so an excessive amount of time wasted on trying to
 * acquire a lock may end up inflicting FPS issues. While this may be annoying for users, it ends up being "dangerous"
 * in regard to the flight model if we drop close to 20 FPS. The maximum amount of total time per cycle needs to aim
 * at sustaining a frame rate of at least 25 or 30 FPS in worst case.
 */
#define POST_PROCESSING_LOCK_TIMEOUT_MILLIS (30)

error_t create_task_schedule(task_schedule_t **task_schedule) {
    *task_schedule = malloc(sizeof(task_schedule_t));
    if (!*task_schedule) {
        return ERROR_MEMORY_ALLOCATION;
    }

    memset(*task_schedule, 0, sizeof(task_schedule_t));

    if (mtx_init(&(*task_schedule)->mutex, mtx_plain|mtx_recursive) != thrd_success) {
        free(*task_schedule);
        *task_schedule = NULL;
        return ERROR_UNSPECIFIC;
    }

    for (int i=0; i<TASK_SCHEDULE_NUM_TASK_QUEUES; i++) {
        (*task_schedule)->queues[i] = create_preallocated_list();
        if (!(*task_schedule)->queues[i]) {
            for (int j=0; j<i; j++) {
                destroy_preallocated_list((*task_schedule)->queues[j], free, PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS);
            }

            mtx_destroy(&(*task_schedule)->mutex);
            free(*task_schedule);
            *task_schedule = NULL;
            return ERROR_MEMORY_ALLOCATION;
        }
    }
    
    return ERROR_NONE;
}

error_t destroy_task_schedule(task_schedule_t *task_schedule) {
    error_t err;
    
    err = lock_schedule(task_schedule);
    if (err != ERROR_NONE) {
        return err;
    }

    task_schedule->destruction_pending = true;

    unlock_schedule(task_schedule);
    thrd_yield();

    // lock again (not using lock_schedule as it will deny access) for a final time
    // to make sure noone else is waiting on the lock
    if (mtx_lock(&task_schedule->mutex) != thrd_success) {
        return ERROR_UNSPECIFIC;
    }

    // first try to unschedule all tasks
    for (int phase=0; phase<TASK_SCHEDULE_NUM_TASK_QUEUES; phase++) {
        prealloc_list_t *queue = task_schedule->queues[phase];
        if (!queue) {
            continue;
        }

        prealloc_list_item_t *item = queue->first_in_use_item;
        while (item) {
            task_t *task = item->value;
            if (!task) {
                continue;
            }
            
            prealloc_list_item_t *next_item = item->next_in_use;
            
            error_t task_err = unschedule_task(task_schedule, task, phase);
            if (task_err != ERROR_NONE) {
                err = task_err;
                // TODO: log
            }
            
            item = next_item;
        }
    }

    // then destroy all empty queues; remark error if not empty
    for (int phase=0; phase<TASK_SCHEDULE_NUM_TASK_QUEUES; phase++) {
        prealloc_list_t *queue = task_schedule->queues[phase];
        if (!queue) {
            continue;
        }

        if (queue->size != 0) {
            // TODO: log
            err = TASK_SCHEDULE_ERROR_STILL_SCHEDULED;
        } else {
            destroy_preallocated_list(queue, free, PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS);
            task_schedule->queues[phase] = NULL;
        }
    }

    mtx_unlock(&task_schedule->mutex);

    if (err == ERROR_NONE) {
        mtx_destroy(&task_schedule->mutex);
        free(task_schedule);
    }

    return err;
}

error_t lock_schedule(task_schedule_t *task_schedule) {
    if (task_schedule->destruction_pending) {
        return ERROR_DESTRUCTION_PENDING;
    }

    if (mtx_lock(&task_schedule->mutex) != thrd_success) {
        return ERROR_MUTEX_FAILED;
    }

    if (task_schedule->destruction_pending) {
        mtx_unlock(&task_schedule->mutex);
        return ERROR_DESTRUCTION_PENDING;
    }

    return ERROR_NONE;
}

error_t lock_schedule_try(task_schedule_t *task_schedule) {
    if (task_schedule->destruction_pending) {
        return ERROR_DESTRUCTION_PENDING;
    }

    if (mtx_trylock(&task_schedule->mutex) != thrd_success) {
        return ERROR_MUTEX_FAILED;
    }

    if (task_schedule->destruction_pending) {
        mtx_unlock(&task_schedule->mutex);
        return ERROR_DESTRUCTION_PENDING;
    }

    return ERROR_NONE;
}

error_t lock_schedule_timeout(task_schedule_t *task_schedule, uint16_t timeout_millis) {
    struct timespec max_wait = {0};
    if (!timespec_now_plus_millis(&max_wait, TIME_UTC, timeout_millis)) {
        return ERROR_UNSPECIFIC;
    }

    if (task_schedule->destruction_pending) {
        return ERROR_DESTRUCTION_PENDING;
    }

    int res = mtx_timedlock(&task_schedule->mutex, &max_wait);
    if (res == thrd_timedout) {
        return ERROR_MUTEX_TIMEOUT;
    } else if (res != thrd_success) {
        return ERROR_MUTEX_FAILED;
    }

    if (task_schedule->destruction_pending) {
        mtx_unlock(&task_schedule->mutex);
        return ERROR_DESTRUCTION_PENDING;
    }

    return ERROR_NONE;
}

void unlock_schedule(task_schedule_t *task_schedule) {
    mtx_unlock(&task_schedule->mutex);
}

static void run_tasks_post_processing(task_schedule_t *task_schedule) {
    error_t err = lock_schedule_try(task_schedule);
    if (err == ERROR_MUTEX_FAILED) {
        RCLOG_DEBUG("run_tasks_post_processing: cannot lock immediately, locking with timeout", err);
        thrd_yield();
        err = lock_schedule_timeout(task_schedule, POST_PROCESSING_LOCK_TIMEOUT_MILLIS);
    }

    if (err != ERROR_NONE) {
        RCLOG_WARN("run_tasks_post_processing: failed to lock task schedule; skipping: %d", err);
        return;
    }

    for (int i=0; i<TASK_SCHEDULE_NUM_TASK_QUEUES; i++) {
        prealloc_list_t *queue = task_schedule->queues[i];
        if (!queue) {
            continue;
        }

        for (prealloc_list_item_t *item = queue->first_in_use_item; item; item = item->next_in_use) {
            task_t *task = item->value;
            if (!task) {
                RCLOG_DEBUG("run_tasks_post_processing: skipping null task in schedule");
                continue;
            }

            if (task->on_processing) {
                RCLOG_TRACE("run_tasks_post_processing: calling task %p", task);
                task->on_processing(task, TASK_SCHEDULE_POST_PROCESSING);
            }
        }
    }

    RCLOG_TRACE("run_tasks_post_processing: unlocking schedule");
    unlock_schedule(task_schedule);
}

void run_tasks(task_schedule_t *task_schedule, task_schedule_phase_t phase) {
    if (phase == TASK_SCHEDULE_POST_PROCESSING) {
        run_tasks_post_processing(task_schedule);
        return;
    }
    
    if (phase < 0 || phase >= TASK_SCHEDULE_NUM_TASK_QUEUES) {
        return;
    }

    error_t err = lock_schedule(task_schedule);
    if (err != ERROR_NONE) {
        RCLOG_WARN("run_tasks: failed to lock task schedule: %d", err);
        return;
    }

    prealloc_list_t *queue = task_schedule->queues[phase];
    if (!queue) {
        goto end;
    }

    prealloc_list_item_t *item = queue->first_in_use_item;
    while (item) {
        task_t *task = item->value;
        if (task->on_processing) {
            RCLOG_TRACE("run_tasks: calling task %p for phase %d", task, phase);
            task->on_processing(task, phase);
        }
        item = item->next_in_use;
    }

end:
    unlock_schedule(task_schedule);
}

error_t schedule_task(task_schedule_t *task_schedule, task_t *task, task_schedule_phase_t phase) {
    if (!task) {
        return ERROR_UNSPECIFIC;
    }
    
    if (phase < 0 || phase >= TASK_SCHEDULE_NUM_TASK_QUEUES) {
        return TASK_SCHEDULE_ERROR_INVALID_PHASE;
    }

    if (task_schedule->destruction_pending) {
        return ERROR_DESTRUCTION_PENDING;
    }

    if (!prealloc_list_append(task_schedule->queues[phase], task)) {
        return ERROR_UNSPECIFIC;
    }

    return ERROR_NONE;
}

static error_t unschedule_task_queue_item(task_schedule_t *task_schedule, prealloc_list_item_t *item, task_schedule_phase_t phase) {
    if (!prealloc_list_delete_item(task_schedule->queues[phase], item, NULL, PREALLOC_ITEM_DEFER_DESTRUCTION)) {
        return ERROR_UNSPECIFIC;
    }
    
    return ERROR_NONE;
}

error_t unschedule_task(task_schedule_t *task_schedule, task_t *task, task_schedule_phase_t phase) {
    if (phase < 0 || phase >= TASK_SCHEDULE_NUM_TASK_QUEUES) {
        return TASK_SCHEDULE_ERROR_INVALID_PHASE;
    }
    
    prealloc_list_t *queue = task_schedule->queues[phase];
    if (!queue) {
        return ERROR_UNSPECIFIC;
    }

    prealloc_list_item_t *item = queue->first_in_use_item;
    while (item) {
        if (item->value == task) {
            RCLOG_TRACE("unschedule_task_queue_item task %p / phase %d", task, phase);
            error_t task_err = unschedule_task_queue_item(task_schedule, item, phase);
            if (task_err == ERROR_NONE) {
                return ERROR_NONE;
            } else {
                RCLOG_WARN("unschedule_task_queue_item failed to unschedule task %p / phase %d: %d", task, phase, task_err);
                return task_err;
            }
        }

        item = item->next_in_use;
    }
    
    return ERROR_UNSPECIFIC;
}

error_t clean_schedule(task_schedule_t *task_schedule) {
    bool success = true;
    
    if (task_schedule->destruction_pending) {
        return ERROR_DESTRUCTION_PENDING;
    }

    for (int i=0; i<TASK_SCHEDULE_NUM_TASK_QUEUES; i++) {
        // FIXME: previously changed to call free in 2d480561 to fix a memleak, however that results in double-frees
        //        because each command's ..._terminate function usually frees the memory - check more thorougly which
        //        command or stuck setup deviates from that routine; alternatively always free here (late) instead
        //        of during termination; both isn't possible.
        if (!prealloc_list_compact(task_schedule->queues[i], NULL, PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS)) {
            success = false;
        }
    }
    
    return success ? ERROR_NONE : ERROR_UNSPECIFIC;
}
