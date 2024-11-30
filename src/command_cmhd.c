#include "command_cmhd.h"

#include <string.h>

#include <XPLMUtilities.h>

#include "lists.h"
#include "logger.h"
#include "session.h"
#include "utils.h"
#include "xptypes.h"

#define CMHD_TASK_PHASE TASK_SCHEDULE_BEFORE_FLIGHT_MODEL

typedef struct {
    char *name;
    bool held;
    XPLMCommandRef xp_ref;
} cmhd_entry_t;

typedef struct {
    session_t *session;
    channel_id_t channel_id;
    
    task_t *task;

    bool initialized;
    bool failed;

    list_t *entries;
} command_cmhd_t;

static const char *cmhd_supported_options[] = {
    NULL
};

static void destroy_entry(void *value) {
    cmhd_entry_t *entry = value;
    
    if (entry->name) {
        free(entry->name);
        entry->name = NULL;
    }

    free(entry);
}

static error_t cmhd_destroy(void *command_ref) {
    RCLOG_TRACE("[CMHD] destroy");
    
    if (!command_ref) {
        return ERROR_UNSPECIFIC;
    }
    
    command_cmhd_t *command = command_ref;

    if (command->entries) {
        // report if there are any held commands that will not be released
        list_item_t *item = command->entries->head;
        while (item) {
            cmhd_entry_t *entry = item->value;
            if (entry->held) {
                RCLOG_WARN("[CMHD] destroying list although XP command is still being held: %s", entry->name);
            }
            item = item->next;
        }
        
        destroy_list(command->entries, destroy_entry);
        command->entries = NULL;
    }

    RCLOG_TRACE("[CMHD] destroy: freeing command");
    free(command);
    
    RCLOG_TRACE("[CMHD] destroy: done");

    return ERROR_NONE;
}

static error_t cmhd_unschedule(command_cmhd_t *command) {
    if (!command || !command->task) {
        return ERROR_NONE;
    }
    
    task_schedule_t *task_schedule = command->session->server->config.task_schedule;
        
    error_t err = lock_schedule(task_schedule);
    if (err == ERROR_NONE) {
        err = unschedule_task(task_schedule, command->task, CMHD_TASK_PHASE);
        unlock_schedule(task_schedule);
    }
        
    if (err != ERROR_NONE) {
        RCLOG_WARN("[CMHD] failed to unschedule task: %d", err);
        return err;
    }

    // just drop the reference but don't free the task; memory management is taken care of by schedule maintenance
    command->task = NULL;

    return ERROR_NONE;
}

static error_t cmhd_terminate(void *command_ref) {
    RCLOG_TRACE("[CMHD] terminate");
    
    if (!command_ref) {
        return ERROR_UNSPECIFIC;
    }
    
    error_t err = ERROR_NONE;
    command_cmhd_t *command = command_ref;

    // channel may have been closed before (by error or finishing); ignore error
    finish_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, NULL);

    err = cmhd_unschedule(command);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[CMHD] terminate failed to unschedule task: %d", err);
        return err;
    }
    
    xpqueue_t *xpqueue = command->session->server->config.xpqueue;

    list_item_t *item = command->entries->tail;
    while (item) {
        list_item_t *prev = item->prev;
        cmhd_entry_t *entry = item->value;

        if (entry->held) {
            error_t err = queue_XPLMCommandEnd(xpqueue, entry->xp_ref);
            if (err == ERROR_NONE) {
                entry->held = false;
                list_delete_item(command->entries, item, destroy_entry);
                entry = NULL;
            } else {
                // although we failed it makes more sense trying to release the remaining commands
                // we still hold, even if that changes the order in which we release them
                // (otherwise we may never release them which could cause even more severe issues)
                RCLOG_WARN("[CMHD] failed to queue XPLMCommandEnd (error %d) for: %s", err, entry->name);
            }
        }
        
        item = prev;
    }

    if (command->entries->size > 0) {
        RCLOG_WARN("[CMHD] %d commands could not be queued for release", command->entries->size);
        return ERROR_UNSPECIFIC;
    }

    channel_id_t channel_id = command->channel_id;
    
    RCLOG_TRACE("[CMHD] terminate: poisoning channel ID");
    command->channel_id = BAD_CHANNEL_ID;
    
    request_channel_destruction(command->session->channels, channel_id);
    
    return ERROR_NONE;
}

static void cmhd_process_flightloop(command_cmhd_t *command) {
    if (command->failed || command->initialized) {
        return;
    }

    // find all commands first; avoid issueing XPLMCommandBegin events if this XPRC command is known to fail
    list_item_t *item = command->entries->head;
    while (item) {
        cmhd_entry_t *entry = item->value;
        
        entry->xp_ref = XPLMFindCommand(entry->name);
        if (entry->xp_ref == NO_XP_COMMAND) {
            command->failed = true;
            RCLOG_DEBUG("[CMHD] XP command not found: %s", entry->name);
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "XP command not found");
            return;
        }
        
        item = item->next;
    }

    // all commands were found; acknowledge command and start holding them
    error_t err = confirm_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, NULL);
    if (err != ERROR_NONE) {
        // ... but stop if we are unable to notify (channel might have been closed)
        command->failed = true;
        return;
    }
    
    item = command->entries->head;
    while (item) {
        cmhd_entry_t *entry = item->value;

        XPLMCommandBegin(entry->xp_ref); // XPLM call has no error feedback
        entry->held = true;
        
        item = item->next;
    }

    command->initialized = true;
}

static void cmhd_process_post(command_cmhd_t *command) {
    if (command->failed) {
        cmhd_terminate(command);
        return;
    }
    
    if (!command->initialized) {
        return;
    }

    // This should only be called once after initialization.
    // As this command remains just idle from now on, we can unschedule the task.

    error_t err = cmhd_unschedule(command);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[CMHD] failed to unschedule in post-process callback");
        command->failed = true;
    }
}

static void cmhd_process(task_t *task, task_schedule_phase_t phase) {
    command_cmhd_t *command = task->reference;

    if (phase == CMHD_TASK_PHASE) {
        cmhd_process_flightloop(command);
    } else if (phase == TASK_SCHEDULE_POST_PROCESSING) {
        cmhd_process_post(command);
    }
}

static error_t cmhd_create(void **command_ref, session_t *session, request_t *request) {
    error_t err = ERROR_NONE;
    error_t out_error = ERROR_NONE;
    
    channel_id_t channel_id = request->channel_id;
    
    if (!request_has_only_options(request, (char**)cmhd_supported_options)) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unsupported options");
        return ERROR_UNSPECIFIC;
    }
    
    command_cmhd_t *command = zalloc(sizeof(command_cmhd_t));
    if (!command) {
        return ERROR_MEMORY_ALLOCATION;
    }

    command->session = session;
    command->channel_id = channel_id;

    command_parameter_t *parameter = request->parameters;
    if (!parameter) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "at least one command is required");
        goto error;
    }

    command->entries = create_list();
    if (!command->entries) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "failed to allocate XP command list");
        out_error = ERROR_MEMORY_ALLOCATION;
        goto error;
    }
    
    while (parameter) {
        cmhd_entry_t *entry = zalloc(sizeof(cmhd_entry_t));
        if (!entry) {
            out_error = ERROR_MEMORY_ALLOCATION;
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "failed to allocate XP command entry");
            goto error;
        }
        
        entry->name = copy_unescaped_string(parameter->parameter);
        if (!entry->name) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, NULL);
            destroy_entry(entry);
            goto error;
        } else if (strlen(entry->name) < 1) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "XP command names must not be empty");
            destroy_entry(entry);
            goto error;
        }

        if (!list_append(command->entries, entry)) {
            out_error = ERROR_MEMORY_ALLOCATION;
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "failed to allocate XP command list item");
            destroy_entry(entry);
            goto error;
        }

        parameter = parameter->next;
    }

    task_t *task = zalloc(sizeof(task_t));
    if (!task) {
        out_error = ERROR_MEMORY_ALLOCATION;
        goto error;
    }
    task->on_processing = cmhd_process;
    task->reference = command;

    err = lock_schedule(session->server->config.task_schedule);
    if (err == ERROR_NONE) {
        err = schedule_task(session->server->config.task_schedule, task, CMHD_TASK_PHASE);
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
    cmhd_destroy(command);
    return (out_error != ERROR_NONE) ? out_error : ERROR_UNSPECIFIC;
}

command_t command_cmhd = {
    .name = "CMHD",
    .create = cmhd_create,
    .terminate = cmhd_terminate,
    .destroy = cmhd_destroy,
};
