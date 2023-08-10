#ifndef TASK_H
#define TASK_H

/**
 * @file task.h task definition as used by task_schedule.h
 *
 *
 */

typedef struct _task_t task_t;

#include "session.h"

/**
 * Called by the task scheduler for the requested phases; processing times should be kept as short as possible,
 * especially in X-Plane phases.
 * @param task reference to the task being called back
 * @param phase current phase; see TASK_SCHEDULE_* constants in task_schedule.h
 */
typedef void (*task_callback2_f) (task_t *task, task_schedule_phase_t phase);

/// a task as used by task_schedule.h
typedef struct _task_t {
    /// called for scheduled phase(s) + post-processing
    task_callback2_f on_processing;
    /// used to provide context to the callback; will not be freed by scheduler!
    void *reference;
} task_t;

// FIXME: task->reference is potentially unsafe to access if unscheduling fails (may get dereferenced although the referenced memory has been freed)
// FIXME: task->reference is not freed by clean_schedule; there probably should be an option to register a deferred destructor
// TODO: task->reference can also not be safely manipulated as there is no way to lock the task
// NOTE: current operations may be safe as only commands are referenced and command termination usually fails if the task cannot be unscheduled; needs to be checked
// TODO: add helper functions (create, destroy, lock/unlock, ...) for task_t

#endif
