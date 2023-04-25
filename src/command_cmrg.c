#include "command_cmrg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <XPLMUtilities.h>

#include "arrays.h"
#include "protocol.h"
#include "session.h"
#include "utils.h"
#include "xptypes.h"

#define CMRG_INVALID 127

#define CMRG_TASK_PHASE TASK_SCHEDULE_BEFORE_FLIGHT_MODEL

// TODO: protect termination from concurrent event calls

typedef struct {
    session_t *session;
    channel_id_t channel_id;
    task_t *task;

    xpcommand_t *xpcmd;
    
    bool initialized;
    bool failed;
} command_cmrg_t;

static error_t cmrg_destroy(void *command_ref) {
    printf("[XPRC] [CMRG] destroy\n"); // DEBUG
    
    if (!command_ref) {
        return ERROR_UNSPECIFIC;
    }
    
    command_cmrg_t *command = command_ref;
    
    printf("[XPRC] [CMRG] destroy: freeing command\n"); // DEBUG
    free(command);
    
    printf("[XPRC] [CMRG] destroy: done\n"); // DEBUG

    return ERROR_NONE;
}

static error_t cmrg_terminate(void *command_ref) {
    printf("[XPRC] [CMRG] terminate\n"); // DEBUG
    
    if (!command_ref) {
        return ERROR_UNSPECIFIC;
    }
    
    error_t err = ERROR_NONE;
    command_cmrg_t *command = command_ref;

    // channel may have been closed before (by error); ignore error
    finish_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, NULL);

    if (command->xpcmd) {
        err = drop_xpcommand(command->xpcmd);
        if (err != ERROR_NONE) {
            printf("[XPRC] [CMRG] terminate: failed to drop XP command, unable to terminate: %s\n", command->xpcmd->name);
            return err;
        }
    }

    err = lock_schedule(command->session->server->config.task_schedule);
    if (err == ERROR_NONE) {
        err = unschedule_task(command->session->server->config.task_schedule, command->task, CMRG_TASK_PHASE);
        if (err == ERROR_NONE) {
            free(command->task);
            command->task = NULL;
        }
        unlock_schedule(command->session->server->config.task_schedule);
    }
    if (err != ERROR_NONE) {
        printf("[XPRC] [CMRG] terminate: failed to unschedule task (error %d) for %s\n", err, command->xpcmd->name);
        return err;
    }
    
    channel_id_t channel_id = command->channel_id;
    
    printf("[XPRC] [CMRG] terminate: poisoning channel ID\n"); // DEBUG
    command->channel_id = BAD_CHANNEL_ID;
    
    request_channel_destruction(command->session->channels, channel_id);
    
    return ERROR_NONE;
}

static void cmrg_initialize(command_cmrg_t *command) {
    printf("[CMRG] initializing %s\n", command->xpcmd->name); // DEBUG
    
    error_t err = register_xpcommand(command->xpcmd);
    printf("[CMRG] registration %d\n", err); // DEBUG
    if (err != ERROR_NONE) {
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "failed to register command");
        command->failed = true;
    } else if (confirm_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, NULL) != ERROR_NONE) {
        if (unregister_destroy_xpcommand(command->xpcmd) == ERROR_NONE) {
            command->xpcmd = NULL;
        } else {
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "channel confirmation and command deregistration failed");
        }
        
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "channel confirmation failed");
        command->failed = true;
    } else {
        command->initialized = true;
    }

    printf("[CMRG] done: initialized=%d, failed=%d\n", command->initialized, command->failed); // DEBUG
}

static void cmrg_process(task_t *task, task_schedule_phase_t phase) {
    command_cmrg_t *command = task->reference;

    if (phase == CMRG_TASK_PHASE) {
        if (!command->initialized && !command->failed) {
            cmrg_initialize(command);
        }
    } else if (command->failed) {
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, NULL);
        cmrg_terminate(command);
    }
}

static void cmrg_handle_event(void *ref, XPLMCommandPhase xp_phase) {
    command_cmrg_t *command = ref;
    if (!command || command->failed) {
        return;
    }

    char *msg = NULL;
    switch (xp_phase) {
    case xplm_CommandBegin: msg = "HOLD"; break;
    case xplm_CommandContinue: msg = "RETRIGGER"; break;
    case xplm_CommandEnd: msg = "RELEASE"; break;
    default: break;
    }

    if (!msg) {
        printf("[CMRG] unhandled XPLMCommandPhase %d: %s\n", xp_phase, command->xpcmd->name);
        command->failed = true;
    } else if (continue_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, msg) != ERROR_NONE) {
        printf("[CMRG] failed to notify: %s\n", command->xpcmd->name);
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, NULL);
        command->failed = true;
    }
}

static xpcommand_propagation_t parse_propagation(char *s) {
    if (!s) {
        return CMRG_INVALID;
    }

    if ((strlen(s) == 5) && !strncmp(s, "false", 5)) {
        return XPCOMMAND_PROPAGATE_STOP;
    } else if ((strlen(s) == 4) && !strncmp(s, "true", 4)) {
        return XPCOMMAND_PROPAGATE_CONTINUE;
    }

    return CMRG_INVALID;
}

static xpcommand_phase_t parse_phase(char *s) {
    if (!s) {
        return CMRG_INVALID;
    }

    if ((strlen(s) == 6) && !strncmp(s, "before", 6)) {
        return XPCOMMAND_PHASE_BEFORE;
    } else if ((strlen(s) == 5) && !strncmp(s, "after", 5)) {
        return XPCOMMAND_PHASE_AFTER;
    }

    return CMRG_INVALID;
}

static error_t cmrg_create(void **command_ref, session_t *session, request_t *request) {
    error_t err = ERROR_NONE;
    error_t out_error = ERROR_NONE;
    char *name = NULL;
    char *description = NULL;
    
    channel_id_t channel_id = request->channel_id;
    
    command_cmrg_t *command = zalloc(sizeof(command_cmrg_t));
    if (!command) {
        return ERROR_MEMORY_ALLOCATION;
    }

    command->session = session;
    command->channel_id = channel_id;

    xpcommand_phase_t phase = parse_phase(request_get_option(request, "phase", "before"));
    if (phase == CMRG_INVALID) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unsupported phase");
        goto error;
    }

    xpcommand_propagation_t propagate = parse_propagation(request_get_option(request, "propagate", "false"));
    if (propagate == CMRG_INVALID) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unsupported propagate option");
        goto error;
    }
    
    command_parameter_t *parameter = request->parameters;
    name = copy_unescaped_string(parameter ? parameter->parameter : NULL);
    if (!name) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "command name is required");
        goto error;
    }

    parameter = parameter->next;
    description = copy_unescaped_string(parameter ? parameter->parameter : name);
    if (!description) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "failed to copy command description");
        goto error;
    }
    
    if (parameter && parameter->next) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "too many parameters");
        goto error;
    }

    command->xpcmd = create_xpcommand(session->server->config.xpcommand_registry, name, description, phase, propagate, command, cmrg_handle_event);
    if (!command->xpcmd) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "failed to prepare XP command");
        goto error;
    }
    
    free(name);
    name = NULL;

    if (description) {
        free(description);
        description = NULL;
    }

    task_t *task = zalloc(sizeof(task_t));
    if (!task) {
        out_error = ERROR_MEMORY_ALLOCATION;
        goto error;
    }
    task->on_processing = cmrg_process;
    task->reference = command;

    err = lock_schedule(session->server->config.task_schedule);
    if (err == ERROR_NONE) {
        err = schedule_task(session->server->config.task_schedule, task, CMRG_TASK_PHASE);
        if (err == ERROR_NONE) {
            command->task = task;
        }
        unlock_schedule(session->server->config.task_schedule);
    }
    
    if (err != ERROR_NONE) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "failed to schedule task");
        free(task);
        out_error = err;
        goto error;
    }

    *command_ref = command;
    
    return ERROR_NONE;

 error:
    if (name) {
        free(name);
    }

    if (description) {
        free(description);
    }
    
    cmrg_destroy(command);
    
    return (out_error != ERROR_NONE) ? out_error : ERROR_UNSPECIFIC;
}

command_t command_cmrg = {
    .name = "CMRG",
    .create = cmrg_create,
    .terminate = cmrg_terminate,
    .destroy = cmrg_destroy,
};
