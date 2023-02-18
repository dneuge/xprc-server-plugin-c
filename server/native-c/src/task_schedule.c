#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include "task_schedule.h"

error_t create_task_schedule(task_schedule_t **task_schedule) {
    *task_schedule = malloc(sizeof(task_schedule_t));
    if (!*task_schedule) {
        return ERROR_MEMORY_ALLOCATION;
    }

    memset(*task_schedule, 0, sizeof(task_schedule_t));

    if (mtx_init(&(*task_schedule)->mutex, mtx_plain) != thrd_success) {
        free(*task_schedule);
        *task_schedule = NULL;
        return ERROR_UNSPECIFIC;
    }

    for (int i=0; i<TASK_SCHEDULE_NUM_TASK_QUEUES; i++) {
        (*task_schedule)->queues[i] = create_preallocated_list();
        if (!(*task_schedule)->queues[i]) {
            for (int j=0; j<i; j++) {
                destroy_preallocated_list((*task_schedule)->queues[j], NULL, PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS);
                mtx_destroy(&(*task_schedule)->mutex);
                free(*task_schedule);
                *task_schedule = NULL;
                return ERROR_MEMORY_ALLOCATION;
            }
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
        return TASK_SCHEDULE_ERROR_DESTRUCTION_PENDING;
    }

    if (mtx_lock(&task_schedule->mutex) != thrd_success) {
        return ERROR_LOCK_FAILED;
    }

    if (task_schedule->destruction_pending) {
        mtx_unlock(&task_schedule->mutex);
        return TASK_SCHEDULE_ERROR_DESTRUCTION_PENDING;
    }

    return ERROR_NONE;
}

void unlock_schedule(task_schedule_t *task_schedule) {
    mtx_unlock(&task_schedule->mutex);
}

static void run_tasks_post_processing(task_schedule_t *task_schedule) {
    for (int i=0; i<TASK_SCHEDULE_NUM_TASK_QUEUES; i++) {
        prealloc_list_t *queue = task_schedule->queues[i];
        if (!queue) {
            continue;
        }

        prealloc_list_item_t *item = queue->first_in_use_item;
        while (item) {
            prealloc_list_item_t *next = item->next_in_use;
            task_t *task = item->value;

            if (task->on_processing) {
                task->on_processing(task, TASK_SCHEDULE_POST_PROCESSING);
            }
            
            item = next;
        }
    }
}

void run_tasks(task_schedule_t *task_schedule, task_schedule_phase_t phase) {
    if (task_schedule->destruction_pending) {
        return;
    }
    
    if (phase == TASK_SCHEDULE_POST_PROCESSING) {
        run_tasks_post_processing(task_schedule);
        return;
    }
    
    if (phase < 0 || phase >= TASK_SCHEDULE_NUM_TASK_QUEUES) {
        return;
    }

    prealloc_list_t *queue = task_schedule->queues[phase];
    if (!queue) {
        return;
    }

    prealloc_list_item_t *item = queue->first_in_use_item;
    while (item) {
        task_t *task = item->value;
        if (task->on_processing) {
            task->on_processing(task, phase);
        }
        item = item->next_in_use;
    }
}

error_t schedule_task(task_schedule_t *task_schedule, task_t *task, task_schedule_phase_t phase) {
    if (!task) {
        return ERROR_UNSPECIFIC;
    }
    
    if (phase < 0 || phase >= TASK_SCHEDULE_NUM_TASK_QUEUES) {
        return TASK_SCHEDULE_ERROR_INVALID_PHASE;
    }

    if (task_schedule->destruction_pending) {
        return TASK_SCHEDULE_ERROR_DESTRUCTION_PENDING;
    }

    if (!prealloc_list_append(task_schedule->queues[phase], task)) {
        return ERROR_UNSPECIFIC;
    }

    return ERROR_NONE;
}

static void destroy_via_task_callback(void *instance) {
    task_t *task = instance;
    if (task->destructor) {
        task->destructor(instance);
    }

    // TODO: log if not set => memleak
}

static error_t unschedule_task_queue_item(task_schedule_t *task_schedule, prealloc_list_item_t *item, task_schedule_phase_t phase) {
    task_t *task = item->value;

    if (task->on_unscheduling) {
        task->on_unscheduling(task, phase);
    }
    
    if (!prealloc_list_delete_item(task_schedule->queues[phase], item, destroy_via_task_callback, PREALLOC_ITEM_DEFER_DESTRUCTION)) {
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
            error_t task_err = unschedule_task_queue_item(task_schedule, item, phase);
            if (task_err == ERROR_NONE) {
                return ERROR_NONE;
            } else {
                // TODO: log
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
        return TASK_SCHEDULE_ERROR_DESTRUCTION_PENDING;
    }

    for (int i=0; i<TASK_SCHEDULE_NUM_TASK_QUEUES; i++) {
        // TODO: report memleak
        if (!prealloc_list_compact(task_schedule->queues[i], destroy_via_task_callback, PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS)) {
            success = false;
        }
    }
    
    return success ? ERROR_NONE : ERROR_UNSPECIFIC;
}

error_t unschedule_session_tasks(task_schedule_t *task_schedule, session_t *session) {
    error_t err = ERROR_NONE;
    
    for (int phase=0; phase<TASK_SCHEDULE_NUM_TASK_QUEUES; phase++) {
        prealloc_list_t *queue = task_schedule->queues[phase];
        if (!queue) {
            continue;
        }

        prealloc_list_item_t *item = queue->first_in_use_item;
        while (item) {
            prealloc_list_item_t *next = item->next_in_use;
            
            task_t *task = item->value;
            if (task->session == session) {
                error_t task_err = unschedule_task_queue_item(task_schedule, item, phase);
                if (task_err != ERROR_NONE) {
                    // TODO: log
                    err = task_err;
                }
            }

            item = next;
        }
    }
    
    return err;
}

error_t unschedule_channel_tasks(task_schedule_t *task_schedule, session_t *session, channel_id_t channel_id) {
    error_t err = ERROR_NONE;
    
    for (int phase=0; phase<TASK_SCHEDULE_NUM_TASK_QUEUES; phase++) {
        prealloc_list_t *queue = task_schedule->queues[phase];
        if (!queue) {
            continue;
        }

        prealloc_list_item_t *item = queue->first_in_use_item;
        while (item) {
            prealloc_list_item_t *next = item->next_in_use;
            
            task_t *task = item->value;
            if (task->session == session && task->channel_id == channel_id) {
                error_t task_err = unschedule_task_queue_item(task_schedule, item, phase);
                if (task_err != ERROR_NONE) {
                    // TODO: log
                    err = task_err;
                }
            }

            item = next;
        }
    }
    
    return err;
}
