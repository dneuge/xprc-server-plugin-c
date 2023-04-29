#ifndef XPQUEUE_H
#define XPQUEUE_H

#include <threads.h>

#include <XPLMUtilities.h>

#include "errors.h"
#include "lists.h"

typedef struct {
    // there only is a single action (XPLMCommandEnd) to be queued so far
    XPLMCommandRef xp_ref;
} xpqueue_task_t;

typedef struct _xpqueue_t {
    bool destruction_pending;
    bool has_pending_tasks;
    mtx_t mutex;
    
    list_t *list;
} xpqueue_t;

error_t create_xpqueue(xpqueue_t **queue);
error_t destroy_xpqueue(xpqueue_t *queue);

// queue_* functions are intended to be called without XP context
error_t queue_XPLMCommandEnd(xpqueue_t *queue, XPLMCommandRef xp_ref);

error_t flush_xpqueue(xpqueue_t *queue); // must only be called within XP context

#endif
