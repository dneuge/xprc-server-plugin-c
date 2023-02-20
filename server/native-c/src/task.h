#ifndef TASK_H
#define TASK_H

// break circular dependency in definitions
typedef struct _task_t task_t;

#include "session.h"

typedef void (*task_callback2_f) (task_t *task, task_schedule_phase_t phase);

typedef struct _task_t {
    task_callback2_f on_processing; // called for scheduled phase(s) + post-processing
    void *reference;
} task_t;

#endif
