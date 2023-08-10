#ifndef XPQUEUE_H
#define XPQUEUE_H

/**
 * @file xpqueue.h queue for one-shot X-Plane SDK calls
 *
 * In addition to full, repeatable task scheduling (see task.h and task_schedule.h) some SDK calls are only one-shot
 * calls. They may need to be queueable quicker, for example during command termination. #xpqueue_t is
 * flushed during xplm_FlightLoop_Phase_AfterFlightModel which executes the actions only once.
 */

#include <threads.h>

#include <XPLMUtilities.h>

#include "errors.h"
#include "lists.h"

/// information required to perform the queued task
typedef struct {
    // there only is a single action (XPLMCommandEnd) to be queued so far
    /// representation of a command in X-Plane as returned by the SDK
    XPLMCommandRef xp_ref;
} xpqueue_task_t;

/// task queue for one-shot X-Plane calls
typedef struct _xpqueue_t {
    /// if destruction is pending (true), no new tasks are allowed to be queued
    bool destruction_pending;
    /// queue has pending tasks when true; queue is empty if set to false
    bool has_pending_tasks;
    /// synchronizes all access within the queue
    mtx_t mutex;

    /// all queued tasks; thread-safety must be ensured for access
    list_t *list;
} xpqueue_t;

/**
 * Creates a new one-shot X-Plane task queue.
 * @param queue will be set to the created instance
 * @return error code; #ERROR_NONE on success
 */
error_t create_xpqueue(xpqueue_t **queue);
/**
 * Destroys the given one-shot X-Plane task queue; queue must have been flushed prior to destruction.
 * @param queue one-shot X-Plane task queue to be destroyed
 * @return error code; #ERROR_NONE on success
 */
error_t destroy_xpqueue(xpqueue_t *queue);

/**
 * Queues a call to XPLMCommandEnd as defined by the X-Plane SDK; queueing does not require an X-Plane context.
 * @param queue queue to add the call to
 * @param xp_ref will be passed to XPLMCommandEnd
 * @return error code; #ERROR_NONE on success
 */
error_t queue_XPLMCommandEnd(xpqueue_t *queue, XPLMCommandRef xp_ref);

/**
 * Executes all queued tasks and unschedules them; to be called by scheduler during
 * xplm_FlightLoop_Phase_AfterFlightModel; requires X-Plane context.
 * @param queue queue to flush
 * @return error code; #ERROR_NONE on success
 */
error_t flush_xpqueue(xpqueue_t *queue);

#endif
