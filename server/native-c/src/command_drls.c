#include "command_drls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <XPLMDataAccess.h>

#include "arrays.h"
#include "session.h"
#include "utils.h"

#define DRLS_SCHEDULE_PHASE TASK_SCHEDULE_BEFORE_FLIGHT_MODEL

static error_t drls_destroy(void *command_ref) {
    printf("[XPRC] [DRLS] destroy\n"); // DEBUG
    
    if (!command_ref) {
        return ERROR_UNSPECIFIC;
    }
    
    command_drls_t *command = command_ref;

    if (command->type == TYPE_SPECIFIC) {
        drls_specific_destroy(command);
    } else {
        drls_unspecific_destroy(command);
    }
    
    printf("[XPRC] [DRLS] destroy: freeing command\n"); // DEBUG
    free(command);
    
    printf("[XPRC] [DRLS] destroy: done\n"); // DEBUG

    return ERROR_NONE;
}

static error_t drls_terminate(void *command_ref) {
    printf("[XPRC] [DRLS] terminate\n"); // DEBUG
    
    if (!command_ref) {
        return ERROR_UNSPECIFIC;
    }
    
    error_t err = ERROR_NONE;
    command_drls_t *command = command_ref;

    // channel may have been closed before (by error or finishing); ignore error
    finish_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, NULL);

    if (command->task) {
        printf("[XPRC] [DRLS] terminate: have task, unscheduling\n"); // DEBUG
        task_schedule_t *task_schedule = command->session->server->config.task_schedule;
        
        err = lock_schedule(task_schedule);
        if (err == ERROR_NONE) {
            err = unschedule_task(task_schedule, command->task, DRLS_SCHEDULE_PHASE);
            unlock_schedule(task_schedule);
        }
        
        if (err != ERROR_NONE) {
            printf("[XPRC] [DRLS] terminate failed to unschedule task: %d\n", err); // DEBUG
            return err;
        }
        
        printf("[XPRC] [DRLS] terminate: freeing task\n"); // DEBUG
        free(command->task);
        command->task = NULL;
    }

    channel_id_t channel_id = command->channel_id;
    
    printf("[XPRC] [DRLS] terminate: poisoning channel ID\n"); // DEBUG
    command->channel_id = BAD_CHANNEL_ID;
    
    request_channel_destruction(command->session->channels, channel_id);
    
    return ERROR_NONE;
}

static void drls_process(task_t *task, task_schedule_phase_t phase) {
    command_drls_t *command = task->reference;

    if (command->failed) {
        drls_terminate(command);
        return;
    }

    if (phase == DRLS_SCHEDULE_PHASE) {
        if (command->type == TYPE_SPECIFIC) {
            drls_specific_process_flightloop(command);
        } else {
            drls_unspecific_process_flightloop(command);
        }
    } else if (phase == TASK_SCHEDULE_POST_PROCESSING) {
        if (command->type == TYPE_SPECIFIC) {
            drls_specific_process_post(command);
        } else {
            drls_unspecific_process_post(command);
        }
    }

    if (command->failed) {
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "failed to process command");
        drls_terminate(command);
        return;
    }

    if (command->done) {
        finish_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, NULL);
        drls_terminate(command);
        return;
    }
}

static int count_parameters(command_parameter_t *parameter) {
    int num = 0;
    while (parameter) {
        num++;
        parameter = parameter->next;
    }
    return num;
}

static error_t drls_create(void **command_ref, session_t *session, request_t *request) {
    error_t err = ERROR_NONE;
    error_t out_error = ERROR_NONE;
    
    int64_t now = millis_since_reference(session);
    if (now < 0) {
        return ERROR_UNSPECIFIC;
    }
    
    channel_id_t channel_id = request->channel_id;
    
    command_drls_t *command = zalloc(sizeof(command_drls_t));
    if (!command) {
        return ERROR_MEMORY_ALLOCATION;
    }

    command->session = session;
    command->channel_id = channel_id;
    
    int num_parameters = count_parameters(request->parameters);
    command->type = (num_parameters > 0) ? TYPE_SPECIFIC : TYPE_UNSPECIFIC;
    if ((command->type == TYPE_UNSPECIFIC) && !COMMAND_DRLS_UNSPECIFIC_SUPPORTED) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "listing all datarefs is not supported by this X-Plane/plugin version");
        goto error;
    }

    out_error = ERROR_UNSPECIFIC;
    if (command->type == TYPE_UNSPECIFIC) {
        out_error = drls_unspecific_create(command);
    } else if (command->type == TYPE_SPECIFIC) {
        out_error = drls_specific_create(command, request);
    }

    if (out_error != ERROR_NONE) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "inner command creation failed");
        goto error;
    }
    
    task_t *task = zalloc(sizeof(task_t));
    if (!task) {
        out_error = ERROR_MEMORY_ALLOCATION;
        goto error;
    }
    task->on_processing = drls_process;
    task->reference = command;

    err = lock_schedule(session->server->config.task_schedule);
    if (err == ERROR_NONE) {
        err = schedule_task(session->server->config.task_schedule, task, DRLS_SCHEDULE_PHASE);
        if (err == ERROR_NONE) {
            command->task = task;
        }
        unlock_schedule(session->server->config.task_schedule);
    }
    
    if (err != ERROR_NONE) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "failed to schedule task");
        out_error = err;
        goto error;
    }

    confirm_channel(session, channel_id, CURRENT_TIME_REFERENCE, NULL);
    
    *command_ref = command;
    
    return ERROR_NONE;

 error:
    if (command) {
        if (command->type == TYPE_UNSPECIFIC) {
            drls_unspecific_destroy(command);
        } else if (command->type == TYPE_SPECIFIC) {
            drls_specific_destroy(command);
        }
        
        free(command);
    }
    return (out_error != ERROR_NONE) ? out_error : ERROR_UNSPECIFIC;
}

command_t command_drls = {
    .name = "DRLS",
    .create = drls_create,
    .terminate = drls_terminate,
    .destroy = drls_destroy,
};
