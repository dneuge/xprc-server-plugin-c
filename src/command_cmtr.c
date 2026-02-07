#include "command_cmtr.h"

#include <string.h>

#include <XPLMUtilities.h>

#include "lists.h"
#include "logger.h"
#include "session.h"
#include "utils.h"
#include "xptypes.h"

#define INFINITE_REPETITION -2134896

#define CMTR_TASK_PHASE TASK_SCHEDULE_BEFORE_FLIGHT_MODEL

#define CMTR_INVALID 0

#define CMTR_MONITOR_HOLD_RELEASE 1
#define CMTR_MONITOR_CYCLE 2
#define CMTR_MONITOR_DISABLE 3
typedef uint8_t cmtr_monitor_mode_t;

#define CMTR_ACTION_HOLD_OR_TRIGGER true
#define CMTR_ACTION_RELEASE false
typedef bool cmtr_action_t;

typedef struct {
    char *name;
    bool held;
    XPLMCommandRef xp_ref;
} cmtr_entry_t;

typedef struct {
    session_t *session;
    channel_id_t channel_id;
    
    task_t *task;

    cmtr_monitor_mode_t monitor_mode;

    bool requires_time;
    
    int32_t interval;
    int64_t interval_due_time;
    int32_t interval_remaining_frames;
    bool is_interval_frames;
    
    int32_t duration;
    int64_t duration_due_time;
    int32_t duration_remaining_frames;
    bool is_duration_frames;

    int times_remaining;
    
    bool initialized;
    bool failed;
    bool finishing;
    
    bool holding; // true if any commands are currently held, i.e. duration has not passed

    list_t *entries;
} command_cmtr_t;

static const char *cmtr_supported_options[] = {
    "hold",
    "monitor",
    "repeatFreq",
    "times",
    NULL
};

static void destroy_entry(void *value) {
    cmtr_entry_t *entry = value;
    
    if (entry->name) {
        free(entry->name);
        entry->name = NULL;
    }

    free(entry);
}

static error_t cmtr_destroy(void *command_ref) {
    RCLOG_TRACE("[CMTR] destroy");
    
    if (!command_ref) {
        return ERROR_UNSPECIFIC;
    }
    
    command_cmtr_t *command = command_ref;

    if (command->entries) {
        // report if there are any held commands that will not be released
        list_item_t *item = command->entries->head;
        while (item) {
            cmtr_entry_t *entry = item->value;
            if (entry->held) {
                RCLOG_WARN("[CMTR] destroying list although XP command is still being held: %s", entry->name);
            }
            item = item->next;
        }
        
        destroy_list(command->entries, destroy_entry);
        command->entries = NULL;
    }

    RCLOG_TRACE("[CMTR] destroy: freeing command");
    free(command);
    
    RCLOG_TRACE("[CMTR] destroy: done");

    return ERROR_NONE;
}

static error_t cmtr_unschedule(command_cmtr_t *command) {
    if (!command || !command->task) {
        return ERROR_NONE;
    }
    
    task_schedule_t *task_schedule = command->session->server->config.task_schedule;
        
    error_t err = lock_schedule(task_schedule);
    if (err == ERROR_NONE) {
        err = unschedule_task(task_schedule, command->task, CMTR_TASK_PHASE);
        unlock_schedule(task_schedule);
    }
        
    if (err != ERROR_NONE) {
        RCLOG_WARN("[CMTR] failed to unschedule task: %d", err);
        return err;
    }

    // just drop the reference but don't free the task; memory management is taken care of by schedule maintenance
    command->task = NULL;

    return ERROR_NONE;
}

static error_t cmtr_terminate(void *command_ref) {
    RCLOG_TRACE("[CMTR] terminate");
    
    if (!command_ref) {
        return ERROR_UNSPECIFIC;
    }
    
    error_t err = ERROR_NONE;
    command_cmtr_t *command = command_ref;

    // channel may have been closed before (by error or finishing); ignore error
    finish_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, NULL);

    err = cmtr_unschedule(command);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[CMTR] terminate failed to unschedule task: %d", err);
        return err;
    }
    
    xpqueue_t *xpqueue = command->session->server->config.xpqueue;

    int failed_to_release = 0;
    list_item_t *item = command->entries->tail;
    while (item) {
        list_item_t *prev = item->prev;
        cmtr_entry_t *entry = item->value;

        if (entry->held) {
            error_t err = queue_XPLMCommandEnd(xpqueue, entry->xp_ref);
            if (err == ERROR_NONE) {
                entry->held = false;
            } else {
                // although we failed it makes more sense trying to release the remaining commands
                // we still hold, even if that changes the order in which we release them
                // (otherwise we may never release them which could cause even more severe issues)
                RCLOG_WARN("[CMTR] failed to queue XPLMCommandEnd (error %d) for: %s", err, entry->name);

                failed_to_release++;
            }
        }
        
        item = prev;
    }

    if (failed_to_release > 0) {
        RCLOG_WARN("[CMTR] %d commands could not be queued for release", failed_to_release);
        return ERROR_UNSPECIFIC;
    }

    channel_id_t channel_id = command->channel_id;
    
    RCLOG_TRACE("[CMTR] terminate: poisoning channel ID");
    command->channel_id = BAD_CHANNEL_ID;
    
    request_channel_destruction(command->session->channels, channel_id);
    
    return ERROR_NONE;
}

static bool cmtr_initialize(command_cmtr_t *command) {
    list_item_t *item = command->entries->head;
    while (item) {
        cmtr_entry_t *entry = item->value;
        
        entry->xp_ref = XPLMFindCommand(entry->name);
        if (entry->xp_ref == NO_XP_COMMAND) {
            command->failed = true;
            RCLOG_DEBUG("[CMTR] XP command not found: %s", entry->name);
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "XP command not found");
            return false;
        }
        
        item = item->next;
    }

    // all commands were found; acknowledge command
    error_t err = confirm_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, NULL);
    if (err != ERROR_NONE) {
        // ... but stop if we are unable to notify (channel might have been closed)
        command->failed = true;
        return false;
    }
    
    command->initialized = true;

    return true;
}

typedef void (*xpref_consumer_f)(XPLMCommandRef xp_ref);

static inline void for_all_xprefs(command_cmtr_t *command, xpref_consumer_f consumer, bool holding, bool only_if_held) {
    for (list_item_t *item = command->entries->head; item; item = item->next) {
        cmtr_entry_t *entry = item->value;

        if (!only_if_held || entry->held) {
            consumer(entry->xp_ref);
        }

        entry->held = holding;
    }
}

static inline void for_all_xprefs_reversed(command_cmtr_t *command, xpref_consumer_f consumer, bool holding, bool only_if_held) {
    for (list_item_t *item = command->entries->tail; item; item = item->prev) {
        cmtr_entry_t *entry = item->value;

        if (!only_if_held || entry->held) {
            consumer(entry->xp_ref);
        }

        entry->held = holding;
    }
}

static inline void notify_channel(command_cmtr_t *command, cmtr_action_t action, bool may_close) {
    bool should_notify = (command->monitor_mode == CMTR_MONITOR_HOLD_RELEASE) || ((action == CMTR_ACTION_HOLD_OR_TRIGGER) && (command->monitor_mode != CMTR_MONITOR_DISABLE));
    if (!should_notify) {
        return;
    }

    char *msg;
    if (action == CMTR_ACTION_HOLD_OR_TRIGGER) {
        if (command->monitor_mode == CMTR_MONITOR_CYCLE) {
            msg = "CYCLE";
        } else {
            msg = "HOLD";
            may_close = false; // final messages can only be CYCLE or RELEASE
        }
    } else {
        msg = "RELEASE";
    }
    
    if (may_close) {
        finish_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, msg);
    } else {
        continue_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, msg);
    }
}

static inline void release_all_held_xprefs(command_cmtr_t *command, bool is_last_action) {
    notify_channel(command, CMTR_ACTION_RELEASE, is_last_action);
    for_all_xprefs_reversed(command, XPLMCommandEnd, false, true);
    command->holding = false;
}

static inline void hold_all_xprefs(command_cmtr_t *command) {
    command->holding = true;
    notify_channel(command, CMTR_ACTION_HOLD_OR_TRIGGER, false);
    for_all_xprefs(command, XPLMCommandBegin, true, false);
}

static inline void trigger_all_xprefs(command_cmtr_t *command, bool is_last_action) {
    if (command->holding) {
        RCLOG_WARN("[CMTR] attempted to trigger while holding - this should not happen!");
        return;
    }
    
    notify_channel(command, CMTR_ACTION_HOLD_OR_TRIGGER, is_last_action);
    for_all_xprefs(command, XPLMCommandOnce, false, false);
    notify_channel(command, CMTR_ACTION_RELEASE, is_last_action);
}

static void cmtr_process_flightloop(command_cmtr_t *command) {
    if (command->failed || command->finishing) {
        return;
    }
    
    int64_t now = 0;
    if (command->requires_time) {
        now = millis_since_reference(command->session);
        if (now < 0) {
            // TODO: fail?
            return;
        }
    }

    bool should_hold = false; // will be reinterpreted as trigger if duration 0 at the end
    bool should_release_before = false; // "before" because it controls release before hold/trigger
    if (!command->initialized) {
        if (!cmtr_initialize(command)) {
            RCLOG_WARN("[CMTR] initialization failed");
            return;
        }
        
        // we always start holding immediately after initialization
        should_hold = true;
    }

    if (command->is_interval_frames) {
        // decrement repetition interval frame counter
        command->interval_remaining_frames--;

        // are we due for another cycle?
        should_hold |= (command->interval_remaining_frames <= 0);
    } else {
        // check if due time of next iteration has been reached
        should_hold |= (now >= command->interval_due_time);
    }

    if (should_hold) {
        // update interval counter/timer, we are about to start a new cycle
        
        if (command->is_interval_frames) {
            // reset the counter
            command->interval_remaining_frames = command->interval;
        } else {
            // forward to next repetition interval
            while (now >= command->interval_due_time) {
                // intervals are always measured relative to when this XPRC command was first created
                command->interval_due_time += command->interval;
            }
        }

        // because we want to start holding XP commands ("begin" API) we first need to release ("end" API)
        // all commands that we may still hold
        should_release_before = command->holding;
    }

    // check if it is time to release any held commands (but avoid unnecessary checks)
    if (!should_release_before && command->holding && (command->duration > 0)) {
        if (command->is_duration_frames) {
            // decrement duration frame counter
            command->duration_remaining_frames--;

            // are we due to release?
            should_release_before = (command->duration_remaining_frames <= 0);
        } else {
            // check if due time for release has been reached
            should_release_before = (now >= command->duration_due_time);
        }
    }
    
    if (should_release_before) {
        // releasing ("end" API) is mutually exclusive to triggering ("once" API),
        // so releasing a previously held XP command is the terminal action in case
        // we reached the requested number of repetitions
        if (command->times_remaining == 0) {
            command->finishing = true;
        }
        
        release_all_held_xprefs(command, command->finishing);
    }

    // either trigger ("once" API) or hold ("begin" API) depending on duration
    if (should_hold) {
        if (command->times_remaining > 0) {
            command->times_remaining--;
        }
        
        if (command->duration > 0) {
            // duration is counted from when we begin to hold the XP commands,
            // reset duration "timers" with the beginning of this new cycle
            if (command->is_duration_frames) {
                command->duration_remaining_frames = command->duration;
            } else {
                command->duration_due_time = now + command->duration;
            }
            
            hold_all_xprefs(command);
        } else {
            // when only triggering (using "once" API) this is the terminal action in case
            // we reached the requested number of repetitions
            if (command->times_remaining == 0) {
                command->finishing = true;
            }
            
            trigger_all_xprefs(command, command->finishing);
        }
    }
}

static void cmtr_process_post(command_cmtr_t *command) {
    if (command->failed || command->finishing) {
        cmtr_terminate(command);
    }
}

static void cmtr_process(task_t *task, task_schedule_phase_t phase) {
    command_cmtr_t *command = task->reference;

    if (phase == CMTR_TASK_PHASE) {
        cmtr_process_flightloop(command);
    } else if (phase == TASK_SCHEDULE_POST_PROCESSING) {
        cmtr_process_post(command);
    }
}

static bool is_suffixed_millis(char *s) {
    return ends_with(s, "ms");
}

static bool is_suffixed_frames(char *s) {
    return ends_with(s, "f");
}

static bool is_valid_interval(char *s) {
    // TODO: check full format (extra characters)
    return is_suffixed_millis(s) || is_suffixed_frames(s);
}

static int parse_times(char *s) {
    if (!s) {
        return -1;
    }
    
    if (!strcmp(s, "infinite")) {
        return INFINITE_REPETITION;
    }

    return atoi(s);
}

static cmtr_monitor_mode_t parse_monitor_mode(char *s) {
    if (!s) {
        return CMTR_INVALID;
    }

    if (!strcmp(s, "cycle")) {
        return CMTR_MONITOR_CYCLE;
    } else if (!strcmp(s, "holdRelease")) {
        return CMTR_MONITOR_HOLD_RELEASE;
    } else if (!strcmp(s, "none")) {
        return CMTR_MONITOR_DISABLE;
    }

    return CMTR_INVALID;
}

static error_t cmtr_create(void **command_ref, session_t *session, request_t *request) {
    error_t err = ERROR_NONE;
    error_t out_error = ERROR_NONE;
    
    channel_id_t channel_id = request->channel_id;

    if (!request_has_only_options(request, (char**)cmtr_supported_options)) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unsupported options");
        return ERROR_UNSPECIFIC;
    }
    
    command_cmtr_t *command = zalloc(sizeof(command_cmtr_t));
    if (!command) {
        return ERROR_MEMORY_ALLOCATION;
    }

    command->session = session;
    command->channel_id = channel_id;

    char *req_duration = request_get_option(request, "hold", "0f");
    if (!is_valid_interval(req_duration)) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "invalid format for hold");
        goto error;
    }

    command->is_duration_frames = is_suffixed_frames(req_duration);
    command->duration = atoi(req_duration);
    if (command->duration < 0) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "invalid value for hold");
        goto error;
    }

    command->times_remaining = parse_times(request_get_option(request, "times", "1"));
    if ((command->times_remaining < 1) && (command->times_remaining != INFINITE_REPETITION)) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "invalid value for times");
        goto error;
    }
    
    char *req_interval = request_get_option(request, "repeatFreq", "1f");
    if (!is_valid_interval(req_interval)) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "invalid format for repeatFreq");
        goto error;
    }

    command->is_interval_frames = is_suffixed_frames(req_interval);
    command->interval = atoi(req_interval);
    if (command->interval <= 0) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "invalid value for repeatFreq");
        goto error;
    }

    if (command->times_remaining == 1) {
        // effectively disable interval if single run is requested
        command->interval = INT32_MAX;
        command->is_interval_frames = false;
    }

    command->requires_time = !command->is_interval_frames || !command->is_duration_frames;
    int64_t now = 0;
    if (command->requires_time) {
        now = millis_since_reference(session);
        if (now < 0) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "failed to retrieve session timestamp");
            goto error;
        }
    }
    
    if (!command->is_interval_frames) {
        // calculate initial repetition due time based on start of XPRC command (i.e. current time)
        command->interval_due_time = now + command->interval;
    }

    command->monitor_mode = parse_monitor_mode(request_get_option(request, "monitor", "cycle"));
    if (command->monitor_mode == CMTR_INVALID) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "invalid monitor option");
        goto error;
    }
    
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
        cmtr_entry_t *entry = zalloc(sizeof(cmtr_entry_t));
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
    task->on_processing = cmtr_process;
    task->reference = command;

    err = lock_schedule(session->server->config.task_schedule);
    if (err == ERROR_NONE) {
        err = schedule_task(session->server->config.task_schedule, task, CMTR_TASK_PHASE);
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
    cmtr_destroy(command);
    return (out_error != ERROR_NONE) ? out_error : ERROR_UNSPECIFIC;
}

command_t command_cmtr = {
    .name = "CMTR",
    .create = cmtr_create,
    .terminate = cmtr_terminate,
    .destroy = cmtr_destroy,
};
