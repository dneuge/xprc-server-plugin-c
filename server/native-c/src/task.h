#ifndef TASK_H
#define TASK_H

// break circular dependency in definitions
typedef struct _task_t task_t;

#include "session.h"

typedef void (*task_callback2_f) (task_t *task, task_schedule_phase_t phase);
typedef void (*task_callback1_f) (task_t *task);

typedef struct _task_t {
    session_t *session;
    channel_id_t channel_id;
    task_callback2_f on_processing; // called for scheduled phase(s) + post-processing
    task_callback2_f on_unscheduling;
    task_callback1_f destructor; // must free task
    void *reference;
} task_t;

#endif
