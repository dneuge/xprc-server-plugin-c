#ifndef TASK_SCHEDULE_H
#define TASK_SCHEDULE_H

#include <stdbool.h>
#include <threads.h>

#include "errors.h"
#include "lists.h"

// early definitions to break circular dependency in definitions
typedef int8_t task_schedule_phase_t;
typedef struct _task_schedule_t task_schedule_t;

#include "task.h"

#define TASK_SCHEDULE_ERROR_DESTRUCTION_PENDING (TASK_SCHEDULE_ERROR_BASE + 0)
#define TASK_SCHEDULE_ERROR_STILL_SCHEDULED     (TASK_SCHEDULE_ERROR_BASE + 1)
#define TASK_SCHEDULE_ERROR_INVALID_PHASE       (TASK_SCHEDULE_ERROR_BASE + 2)

#define TASK_SCHEDULE_POST_PROCESSING -1
#define TASK_SCHEDULE_BEFORE_FLIGHT_MODEL 0
#define TASK_SCHEDULE_AFTER_FLIGHT_MODEL 1
#define TASK_SCHEDULE_NUM_TASK_QUEUES 2

typedef struct _task_schedule_t {
    mtx_t mutex;
    prealloc_list_t *queues[TASK_SCHEDULE_NUM_TASK_QUEUES];
    bool destruction_pending;
} task_schedule_t;

error_t create_task_schedule(task_schedule_t **task_schedule);
error_t destroy_task_schedule(task_schedule_t *task_schedule);

error_t lock_schedule(task_schedule_t *task_schedule);
void unlock_schedule(task_schedule_t *task_schedule);

// all functions below must only be called while holding a lock

void run_tasks(task_schedule_t *task_schedule, task_schedule_phase_t phase);

error_t schedule_task(task_schedule_t *task_schedule, task_t *task, task_schedule_phase_t phase); // one task per phase; callers must create but not free task (will be freed on cleanup after unscheduling)
error_t unschedule_task(task_schedule_t *task_schedule, task_t *task, task_schedule_phase_t phase);
error_t clean_schedule(task_schedule_t *task_schedule);

error_t unschedule_session_tasks(task_schedule_t *task_schedule, session_t *session);
error_t unschedule_channel_tasks(task_schedule_t *task_schedule, session_t *session, channel_id_t channel_id);

#endif
