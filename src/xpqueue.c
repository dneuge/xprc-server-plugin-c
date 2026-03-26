#include <stdlib.h>

#include "logger.h"
#include "utils.h"
#include "xpqueue.h"

#include "xptypes.h"

error_t create_xpqueue(xpqueue_t **queue) {
    if (!queue) {
        RCLOG_ERROR("[xpqueue] create_xpqueue called with NULL");
        return ERROR_UNSPECIFIC;
    }

    *queue = zalloc(sizeof(xpqueue_t));
    if (!(*queue)) {
        return ERROR_MEMORY_ALLOCATION;
    }

    (*queue)->list = create_list();
    if (!(*queue)->list) {
        free(*queue);
        *queue = NULL;
        return ERROR_MEMORY_ALLOCATION;
    }

    if (mtx_init(&(*queue)->mutex, mtx_plain) != thrd_success) {
        destroy_list((*queue)->list, free);
        free(*queue);
        *queue = NULL;
        return ERROR_UNSPECIFIC;
    }

    return ERROR_NONE;
}

static error_t lock_xpqueue(xpqueue_t *queue) {
    if (queue->destruction_pending) {
        return ERROR_DESTRUCTION_PENDING;
    }

    if (mtx_lock(&queue->mutex) != thrd_success) {
        return ERROR_MUTEX_FAILED;
    }

    if (queue->destruction_pending) {
        mtx_unlock(&queue->mutex);
        return ERROR_DESTRUCTION_PENDING;
    }

    return ERROR_NONE;
}

static void unlock_xpqueue(xpqueue_t *queue) {
    mtx_unlock(&queue->mutex);
}

error_t destroy_xpqueue(xpqueue_t *queue) {
    error_t err = ERROR_NONE;

    if (!queue) {
        return ERROR_NONE;
    }

    err = lock_xpqueue(queue);
    if (err != ERROR_NONE) {
        RCLOG_ERROR("[xpqueue] failed to lock queue for destruction, error %d", err);
        return err;
    }

    if (queue->has_pending_tasks || (queue->list->size > 0)) {
        RCLOG_ERROR("[xpqueue] unable to destroy, queue has %d pending tasks remaining", queue->list->size);
        unlock_xpqueue(queue);
        return ERROR_UNSPECIFIC;
    }

    queue->destruction_pending = true;
    unlock_xpqueue(queue);

    if (mtx_lock(&queue->mutex) != thrd_success) {
        RCLOG_WARN("[xpqueue] failed to relock queue for destruction, continuing anyway");
    } else {
        mtx_unlock(&queue->mutex);
    }

    destroy_list(queue->list, free);
    queue->list = NULL;

    mtx_destroy(&queue->mutex);
    
    free(queue);

    return ERROR_NONE;
}

error_t queue_XPLMCommandEnd(xpqueue_t *queue, XPLMCommandRef xp_ref) {
    error_t err = ERROR_NONE;
    error_t out_err = ERROR_NONE;

    if (!queue) {
        RCLOG_ERROR("[xpqueue] queue_XPLMCommandEnd called with queue=%p", queue);
        return ERROR_UNSPECIFIC;
    }

    if (xp_ref == NO_XP_COMMAND) {
        return ERROR_UNSPECIFIC;
    }
    
    xpqueue_task_t *task = zalloc(sizeof(xpqueue_task_t));
    if (!task) {
        return ERROR_MEMORY_ALLOCATION;
    }

    task->xp_ref = xp_ref;

    err = lock_xpqueue(queue);
    if (err != ERROR_NONE) {
        free(task);
        return err;
    }

    if (list_append(queue->list, task)) {
        queue->has_pending_tasks = true;
    } else {
        out_err = ERROR_MEMORY_ALLOCATION;
        free(task);
    }
    
    unlock_xpqueue(queue);

    return out_err;
}

error_t flush_xpqueue(xpqueue_t *queue) {
    // called each cycle from flight loop, thus we quit as early as possible
    // if nothing is to do (has_pending_tasks check without lock)

    error_t err = ERROR_NONE;
    error_t out_err = ERROR_NONE;
    
    if (!queue) {
        RCLOG_WARN("[xpqueue] flush called with NULL queue");
        return ERROR_UNSPECIFIC;
    }

    if (!queue->has_pending_tasks) {
        return ERROR_NONE;
    }

    err = lock_xpqueue(queue);
    if (err != ERROR_NONE) {
        return err;
    }

    list_item_t *item = queue->list->head;
    while (item) {
        list_item_t *next = item->next;
        xpqueue_task_t *task = item->value;

        XPLMCommandEnd(task->xp_ref); // XPLM call has no error feedback
        
        list_delete_item(queue->list, item, free);
        
        item = next;
    }

    if (queue->list->size == 0) {
        queue->has_pending_tasks = false;
    } else {
        RCLOG_WARN("[xpqueue] flush incomplete, %d items remaining", queue->list->size);
        out_err = ERROR_UNSPECIFIC;
    }

    unlock_xpqueue(queue);

    return out_err;
}
