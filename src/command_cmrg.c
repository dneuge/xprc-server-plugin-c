#include "command_cmrg.h"

#include <string.h>

#include <XPLMUtilities.h>

#include "arrays.h"
#include "logger.h"
#include "protocol.h"
#include "session.h"
#include "utils.h"
#include "xptypes.h"

#define CMRG_COMMAND_VERSION 1

#define CMRG_INVALID 127

#define CMRG_TASK_PHASE TASK_SCHEDULE_BEFORE_FLIGHT_MODEL

// TODO: protect termination from concurrent event calls

#define CMRG_MONITOR_TRIGGER      (1 << 0)
#define CMRG_MONITOR_HOLD_RELEASE (1 << 1)
#define CMRG_MONITOR_ALL          (CMRG_MONITOR_TRIGGER | CMRG_MONITOR_HOLD_RELEASE)
typedef uint8_t cmrg_monitor_t;

typedef struct {
    session_t *session;
    channel_id_t channel_id;
    task_t *task;

    cmrg_monitor_t monitor;
    
    xpcommand_t *xpcmd;
    
    bool initialized;
    bool failed;
} command_cmrg_t;

static const char *cmrg_supported_options[] = {
    "monitor",
    "phase",
    "propagate",
    NULL
};

static error_t cmrg_destroy(void *command_ref) {
    RCLOG_TRACE("[CMRG] destroy");
    
    if (!command_ref) {
        return ERROR_UNSPECIFIC;
    }
    
    command_cmrg_t *command = command_ref;
    
    RCLOG_TRACE("[CMRG] destroy: freeing command");
    free(command);
    
    RCLOG_TRACE("[CMRG] destroy: done");

    return ERROR_NONE;
}

static error_t cmrg_terminate(void *command_ref) {
    RCLOG_TRACE("[CMRG] terminate");
    
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
            RCLOG_WARN("[CMRG] terminate: failed to drop XP command, unable to terminate: %s", command->xpcmd->name);
            return err;
        }
    }

    err = lock_schedule(command->session->server->config.task_schedule);
    if (err == ERROR_NONE) {
        err = unschedule_task(command->session->server->config.task_schedule, command->task, CMRG_TASK_PHASE);
        if (err == ERROR_NONE) {
            // just drop the reference but don't free the task; memory management is taken care of by schedule maintenance
            command->task = NULL;
        }
        unlock_schedule(command->session->server->config.task_schedule);
    }
    if (err != ERROR_NONE) {
        RCLOG_WARN("[CMRG] terminate: failed to unschedule task (error %d) for %s", err, command->xpcmd->name);
        return err;
    }
    
    channel_id_t channel_id = command->channel_id;
    
    RCLOG_TRACE("[CMRG] terminate: poisoning channel ID");
    command->channel_id = BAD_CHANNEL_ID;
    
    request_channel_destruction(command->session->channels, channel_id);
    
    return ERROR_NONE;
}

static void cmrg_initialize(command_cmrg_t *command) {
    RCLOG_TRACE("[CMRG] initializing %s", command->xpcmd->name);
    
    error_t err = register_xpcommand(command->xpcmd);
    RCLOG_TRACE("[CMRG] registration %d", err);
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

    RCLOG_TRACE("[CMRG] done: initialized=%d, failed=%d", command->initialized, command->failed);
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
    if (xp_phase == xplm_CommandContinue) {
        if ((command->monitor & CMRG_MONITOR_TRIGGER) != 0) {
            msg = "TRIGGER";
        }
    } else if ((xp_phase == xplm_CommandBegin) || (xp_phase == xplm_CommandEnd)) {
        if ((command->monitor & CMRG_MONITOR_HOLD_RELEASE) != 0) {
            msg = (xp_phase == xplm_CommandBegin) ? "HOLD" : "RELEASE";
        }
    } else {
        RCLOG_WARN("[CMRG] unhandled XPLMCommandPhase %d: %s", xp_phase, command->xpcmd->name);
        command->failed = true;
    }
    
    if (!msg) {
        return;
    }
    
    if (continue_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, msg) != ERROR_NONE) {
        RCLOG_WARN("[CMRG] failed to notify: %s", command->xpcmd->name);
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, NULL);
        command->failed = true;
    }
}

static cmrg_monitor_t parse_monitor(char *s) {
    if (!s) {
        return CMRG_INVALID;
    }

    if (!strcmp(s, "all")) {
        return CMRG_MONITOR_ALL;
    } else if (!strcmp(s, "trigger")) {
        return CMRG_MONITOR_TRIGGER;
    } else if (!strcmp(s, "holdRelease")) {
        return CMRG_MONITOR_HOLD_RELEASE;
    }

    return CMRG_INVALID;
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

static error_t cmrg_create(void **command_ref, session_t *session, request_t *request, command_config_t *config) {
    error_t err = ERROR_NONE;
    error_t out_error = ERROR_NONE;
    char *name = NULL;
    char *description = NULL;
    
    channel_id_t channel_id = request->channel_id;

    if (config->version != CMRG_COMMAND_VERSION) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unexpected command version");
        return ERROR_UNSPECIFIC;
    }

    if (!request_has_only_options(request, (char**)cmrg_supported_options)) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unsupported options");
        return ERROR_UNSPECIFIC;
    }
    
    command_cmrg_t *command = zmalloc(sizeof(command_cmrg_t));
    if (!command) {
        return ERROR_MEMORY_ALLOCATION;
    }

    command->session = session;
    command->channel_id = channel_id;

    command->monitor = parse_monitor(request_get_option(request, "monitor", "all"));
    if (command->monitor == CMRG_INVALID) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unsupported monitor option");
        goto error;
    }
    
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

    task_t *task = zmalloc(sizeof(task_t));
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

static command_config_t* cmrg_create_default_config() {
    return create_command_config(CMRG_COMMAND_VERSION);
}

static error_t cmrg_merge_config(command_config_t **new_config, char **err_msg, command_config_t *previous_config, command_config_t *requested_changes) {
    if (requested_changes->version != CMRG_COMMAND_VERSION) {
        *err_msg = dynamic_sprintf("only supported version is %u, requested %u", CMRG_COMMAND_VERSION, requested_changes->version);
        return ERROR_UNSPECIFIC;
    }

    if (has_command_feature_flags(requested_changes)) {
        *err_msg = dynamic_sprintf("current command implementation does not support any feature flags");
        return ERROR_UNSPECIFIC;
    }

    return ERROR_NONE;
}

command_t command_cmrg = {
    .name = "CMRG",
    .create = cmrg_create,
    .terminate = cmrg_terminate,
    .destroy = cmrg_destroy,
    .create_default_config = cmrg_create_default_config,
    .merge_config = cmrg_merge_config,
};
