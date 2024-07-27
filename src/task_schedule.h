#ifndef TASK_SCHEDULE_H
#define TASK_SCHEDULE_H

/**
 * @file task_schedule.h schedule to coordinate delayed and/or repeated tasks with X-Plane phases
 *
 * This module provides the task schedule and related functions, allowing arbitrary tasks to request execution in
 * X-Plane context if needed. Such elevation is not only necessary for synchronized processing but also because the
 * X-Plane SDK can only be interacted from within X-Plane callbacks (#TASK_SCHEDULE_BEFORE_FLIGHT_MODEL and
 * #TASK_SCHEDULE_AFTER_FLIGHT_MODEL).
 *
 * *Note that this is **not** the scheduler:* Schedule execution is coordinated solely through X-Plane. The plugin
 * registers itself for relevant X-Plane phase callbacks and only invokes #run_tasks() from the correct threads as
 * phases are reached or (in case of #TASK_SCHEDULE_POST_PROCESSING) completed.
 */

#include <stdbool.h>

#ifndef NEED_C11_THREADS_WRAPPER
#include <threads.h>
#else
#include <c11/threads.h>
#endif

#include "errors.h"
#include "lists.h"

/// see TASK_SCHEDULE_*
typedef int8_t task_schedule_phase_t;
typedef struct _task_schedule_t task_schedule_t;

#include "task.h"

/// indicates that the task schedule is not empty and thus cannot be destroyed
#define TASK_SCHEDULE_ERROR_STILL_SCHEDULED     (TASK_SCHEDULE_ERROR_BASE + 0)
/// used when an unknown phase was requested
#define TASK_SCHEDULE_ERROR_INVALID_PHASE       (TASK_SCHEDULE_ERROR_BASE + 1)

/**
 * task schedule phase that happens outside X-Plane thread after the #TASK_SCHEDULE_AFTER_FLIGHT_MODEL; should be used
 * to perform everything that has longer processing times
 */
#define TASK_SCHEDULE_POST_PROCESSING -1
/**
 * task schedule phase equal to xplm_FlightLoop_Phase_BeforeFlightModel in the X-Plane SDK; callbacks happen in X-Plane
 * context and should be finished as fast as possible to minimize the impact on the simulator
 */
#define TASK_SCHEDULE_BEFORE_FLIGHT_MODEL 0
/**
 * task schedule phase equal to xplm_FlightLoop_Phase_AfterFlightModel in the X-Plane SDK; callbacks happen in X-Plane
 * context and should be finished as fast as possible to minimize the impact on the simulator
 */
#define TASK_SCHEDULE_AFTER_FLIGHT_MODEL 1

/// the total number of all task queues used by the schedule
#define TASK_SCHEDULE_NUM_TASK_QUEUES 2

/// holds task queues for all managed schedule phases and provides synchronization
typedef struct _task_schedule_t {
    /// used to synchronize access to task schedule and queues
    mtx_t mutex;
    /// queued tasks per scheduled phase
    prealloc_list_t *queues[TASK_SCHEDULE_NUM_TASK_QUEUES];
    /// if destruction is pending (true) only reduction/clean up is allowed
    bool destruction_pending;
} task_schedule_t;

/**
 * Creates a new task schedule.
 * @param task_schedule will be set to the created instance
 * @return error code; #ERROR_NONE on success
 */
error_t create_task_schedule(task_schedule_t **task_schedule);
/**
 * Destroys a task schedule.
 *
 * The schedule is first set to destruction_pending and relocked to allow all threads to back off from further access
 * and prevent new tasks from being scheduled.
 *
 * Destruction is only completed if all tasks could be unscheduled. It is possible that some tasks fail to unschedule
 * in which case they may continue to be executed by the scheduler. A critical error should be raised if that happens.
 *
 * @param task_schedule task schedule to destroy
 * @return error code; #ERROR_NONE on success
 */
error_t destroy_task_schedule(task_schedule_t *task_schedule);

/**
 * Attempts to lock the given schedule.
 * @param task_schedule task schedule to lock
 * @return true if lock was gained, false if locking failed
 */
error_t lock_schedule(task_schedule_t *task_schedule);
/**
 * Unlocks a previously locked schedule; must only be called if a lock is currently held.
 * @param task_schedule task schedule to unlock
 */
void unlock_schedule(task_schedule_t *task_schedule);

/**
 * To be called by the actual scheduler to run all tasks for the specified phase; schedule must be locked.
 *
 * @param task_schedule task schedule to run tasks of
 * @param phase phase to run tasks for
 */
void run_tasks(task_schedule_t *task_schedule, task_schedule_phase_t phase);

/**
 * Schedules the given task to be run in the specified phase and the post-processing phase until unscheduled;
 * schedule must be locked.
 *
 * If another phase is needed, an additional task needs to be scheduled.
 *
 * If adding the task succeeds, memory management is taken over by the schedule. Such tasks must not be freed
 * outside the schedule's internal functions itself (will be done during #clean_schedule()).
 *
 * @param task_schedule task schedule to add task to
 * @param task task to be added
 * @param phase main phase to schedule the task for; must not be #TASK_SCHEDULE_POST_PROCESSING (tasks are always scheduled for post-processing)
 * @return error code; #ERROR_NONE on success
 */
error_t schedule_task(task_schedule_t *task_schedule, task_t *task, task_schedule_phase_t phase);

/**
 * Removes the given task from the specified phase and the post-processing phase schedule; schedule must be locked.
 *
 * Note that memory management was taken over by the scheduler during #schedule_task() already. The task must not be
 * freed by callers.
 *
 * @param task_schedule task schedule to remove task from
 * @param task task to be removed
 * @param phase main phase to remove the task from; must not be #TASK_SCHEDULE_POST_PROCESSING (post-processing does always get unscheduled)
 * @return error code; #ERROR_NONE on success
 */
error_t unschedule_task(task_schedule_t *task_schedule, task_t *task, task_schedule_phase_t phase);

/**
 * Should be called regularly for garbage collection - compacts internal data structures and frees previously removed
 * tasks; schedule must be locked.
 *
 * @param task_schedule task schedule to clean up
 * @return error code; #ERROR_NONE on success
 */
error_t clean_schedule(task_schedule_t *task_schedule);

#endif
