#include "command_drqv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <XPLMDataAccess.h>

#include "session.h"
#include "utils.h"

#define INFINITE_REPETITION -2134896

typedef struct _drqv_dataref_t drqv_dataref_t;
typedef struct _drqv_dataref_t {
    void *value_buffer;
    size_t value_buffer_size;
    
    char *name;
    XPLMDataTypeID type;
    XPLMDataRef xp_ref;

    drqv_dataref_t *next;
} drqv_dataref_t;

typedef struct {
    session_t *session;
    channel_id_t channel_id;
    
    task_t *task;
    task_schedule_phase_t phase;
    
    int32_t interval;
    int32_t interval_wait;
    bool is_interval_frames;
    int32_t times_remaining;
    int64_t interval_not_before_timestamp;

    bool initialized;
    bool failed;

    drqv_dataref_t *datarefs;
    int64_t timestamp; // last flightloop, 0 if we data was not updated since it has last been sent
} command_drqv_t;

static error_t drqv_destroy(void *command_ref) {
    printf("[XPRC] [DRQV] destroy\n"); // DEBUG
    
    if (!command_ref) {
        return ERROR_UNSPECIFIC;
    }
    
    command_drqv_t *command = command_ref;
    
    drqv_dataref_t *dataref = command->datarefs;
    while (dataref) {
        drqv_dataref_t *next_dataref = dataref->next;
        
        if (dataref->name) {
            printf("[XPRC] [DRQV] destroy: freeing dataref name\n"); // DEBUG
            free(dataref->name);
        }

        if (dataref->value_buffer) {
            printf("[XPRC] [DRQV] destroy: freeing dataref buffer\n"); // DEBUG
            free(dataref->value_buffer);
        }
        
        printf("[XPRC] [DRQV] destroy: freeing dataref\n"); // DEBUG
        free(dataref);
        dataref = next_dataref;
    }
    
    printf("[XPRC] [DRQV] destroy: freeing command\n"); // DEBUG
    free(command);
    
    printf("[XPRC] [DRQV] destroy: done\n"); // DEBUG

    return ERROR_NONE;
}

static error_t drqv_terminate(void *command_ref) {
    printf("[XPRC] [DRQV] terminate\n"); // DEBUG
    
    if (!command_ref) {
        return ERROR_UNSPECIFIC;
    }
    
    error_t err = ERROR_NONE;
    command_drqv_t *command = command_ref;

    // channel may have been closed before (by error or finishing); ignore error
    finish_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, NULL);

    if (command->task) {
        printf("[XPRC] [DRQV] terminate: have task, unscheduling\n"); // DEBUG
        task_schedule_t *task_schedule = command->session->server->config.task_schedule;
        
        err = lock_schedule(task_schedule);
        if (err == ERROR_NONE) {
            err = unschedule_task(task_schedule, command->task, command->phase);
            unlock_schedule(task_schedule);
        }
        
        if (err != ERROR_NONE) {
            printf("[XPRC] [DRQV] terminate failed to unschedule task: %d\n", err); // DEBUG
            return err;
        }
        
        printf("[XPRC] [DRQV] terminate: freeing task\n"); // DEBUG
        free(command->task);
        command->task = NULL;
    }

    channel_id_t channel_id = command->channel_id;
    
    printf("[XPRC] [DRQV] terminate: poisoning channel ID\n"); // DEBUG
    command->channel_id = BAD_CHANNEL_ID;
    
    request_channel_destruction(command->session->channels, channel_id);
    
    return ERROR_NONE;
}

static XPLMDataTypeID parse_type(char *s, int count) {
    if (!strncmp("int", s, count)) {
        return xplmType_Int;
    } else if (!strncmp("float", s, count)) {
        return xplmType_Float;
    } else if (!strncmp("double", s, count)) {
        return xplmType_Double;
    } else if (!strncmp("int[]", s, count)) {
        return xplmType_IntArray;
    } else if (!strncmp("float[]", s, count)) {
        return xplmType_FloatArray;
    } else if (!strncmp("blob", s, count)) {
        return xplmType_Data;
    } else {
        return xplmType_Unknown;
    }
}

static bool drqv_initialize(command_drqv_t *command) {
    drqv_dataref_t *dataref = command->datarefs;
    while (dataref) {
        if (!dataref->value_buffer || dataref->value_buffer_size <= 0) {
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "internal error preparing dataref buffer");
            command->failed = true;
            return false;
        }
        
        dataref->xp_ref = XPLMFindDataRef(dataref->name);
        if (!dataref->xp_ref) {
            printf("[XPRC] [DRQV] XP did not find dataref: \"%s\"\n", dataref->name); // DEBUG
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "dataref does not exist");
            command->failed = true;
            return false;
        }

        XPLMDataTypeID available_types = XPLMGetDataRefTypes(dataref->xp_ref);
        if (!(available_types & dataref->type)) {
            printf("[XPRC] [DRQV] wanted type %d, got types %d\n", dataref->type, available_types); // DEBUG
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "type is not available from dataref");
            command->failed = true;
            return false;
        }
        
        dataref = dataref->next;
    }
    
    command->initialized = true;
    
    return command->initialized;
}

static void drqv_process_flightloop(command_drqv_t *command) {
    if (command->failed) {
        return;
    }

    int64_t now = millis_since_reference(command->session);
    if (now < 0) {
        return;
    }

    bool should_run = false;
    if (!command->initialized) {
        if (!drqv_initialize(command)) {
            command->failed = true;
            return;
        }
        confirm_channel(command->session, command->channel_id, now, NULL);

        // we always run immediately after initialization
        should_run = true;
    } else {
        // check interval
        if (!command->is_interval_frames) {
            // ... based on time
            should_run = (now >= command->interval_not_before_timestamp);
        } else {
            // ... based on (flight loop) frame count
            command->interval_wait--;
            if (command->interval_wait <= 0) {
                should_run = true;
                command->interval_wait = command->interval;
            }
        }
    }

    // skip further updates if we should not run
    if (!should_run) {
        return;
    }

    // if interval is time-based forward to next multiple based on start time
    // (try to maintain interval, avoid progressively slower intervals)
    if (!command->is_interval_frames) {
        while (command->interval_not_before_timestamp <= now) {
            command->interval_not_before_timestamp += command->interval;
        }
    }

    // count repetitions
    if (command->times_remaining > 0) {
        command->times_remaining--;
    } else if (command->times_remaining != INFINITE_REPETITION) {
        return;
    }

    // timestamp data
    command->timestamp = now;

    // update data
    drqv_dataref_t *dataref = command->datarefs;
    while (dataref) {
        switch (dataref->type) {
        case xplmType_Int:
            *((int32_t*) dataref->value_buffer) = XPLMGetDatai(dataref->xp_ref);
            break;
            
        case xplmType_Float:
            *((float*) dataref->value_buffer) = XPLMGetDataf(dataref->xp_ref);
            break;
            
        case xplmType_Double:
            *((double*) dataref->value_buffer) = XPLMGetDatad(dataref->xp_ref);
            break;

        // TODO: add arrays and blob support (get full array, resize buffer if too small)
            
        default:
            error_channel(command->session, command->channel_id, command->timestamp, "unsupported type");
            command->failed = true;
            return;
        }
        
        dataref = dataref->next;
    }
}

static char* encode_value(XPLMDataTypeID type, void *value, size_t value_size) {
    // TODO: extract to helper module
    if (!value || value_size < 1) {
        return NULL;
    }

    char *out = NULL;
    if (type == xplmType_Int && value_size == 4) {
        out = dynamic_sprintf("%d", *((int32_t*) value));
    } else if (type == xplmType_Float && value_size == 4) {
        out = dynamic_sprintf("%f", *((float*) value));
    } else if (type == xplmType_Double && value_size == 8) {
        out = dynamic_sprintf("%f", *((double*) value));
    }

    return out;
}

static char* encode_dataref_value(drqv_dataref_t *dataref) {
    return encode_value(dataref->type, dataref->value_buffer, dataref->value_buffer_size);
}

static void drqv_process_post(command_drqv_t *command) {
    if (command->failed) {
        drqv_terminate(command);
        return;
    }
    
    if (!command->initialized) {
        return;
    }

    if (command->timestamp <= 0) {
        // no data since last run
        return;
    }

    // TODO: optimize: avoid concatenation if only a single dataref is handled
    
    prealloc_list_t *list = create_preallocated_list();
    if (!list) {
        error_channel(command->session, command->channel_id, command->timestamp, "failed to allocate list for value encoding");
        command->failed = true;
        return;
    }

    drqv_dataref_t *dataref = command->datarefs;
    bool is_first = true;
    int total_length = 0;
    while (dataref) {
        if (!is_first) {
            char *separator_copy = copy_string(";");
            if (!separator_copy) {
                error_channel(command->session, command->channel_id, command->timestamp, "failed to encode value");
                command->failed = true;
                destroy_preallocated_list(list, free, PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS);
                return;
            }

            total_length++;
            prealloc_list_append(list, separator_copy);
        }
        
        char *encoded_value = encode_dataref_value(dataref);
        if (!encoded_value) {
            error_channel(command->session, command->channel_id, command->timestamp, "failed to encode value");
            command->failed = true;
            destroy_preallocated_list(list, free, PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS);
            return;
        }

        total_length += strlen(encoded_value);
        prealloc_list_append(list, encoded_value);

        dataref = dataref->next;
    }

    char *out = zalloc(total_length+1);
    if (!out) {
        error_channel(command->session, command->channel_id, command->timestamp, "failed to allocate memory to concatenate encoded values");
        command->failed = true;
        destroy_preallocated_list(list, free, PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS);
        return;
    }
    
    char *dest = out;
    prealloc_list_item_t *item = list->first_in_use_item;
    while (item) {
        int len = strlen(item->value);
        memcpy(dest, item->value, len);
        dest += len;
        item = item->next_in_use;
    }
    
    destroy_preallocated_list(list, free, PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS);

    bool will_continue = false;
    error_t err = ERROR_NONE;
    if (!will_continue) {
        err = continue_channel(command->session, command->channel_id, command->timestamp, out);
    } else {
        finish_channel(command->session, command->channel_id, command->timestamp, out);
        free(out);
        drqv_terminate(command);
        return;
    }
    
    free(out);

    if (err != ERROR_NONE) {
        error_channel(command->session, command->channel_id, command->timestamp, "error on result submission");
        command->failed = true;
    }
    
    command->timestamp = 0;
}

static void drqv_process(task_t *task, task_schedule_phase_t phase) {
    command_drqv_t *command = task->reference;

    if (phase == command->phase) {
        drqv_process_flightloop(command);
    } else if (phase == TASK_SCHEDULE_POST_PROCESSING) {
        drqv_process_post(command);
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

static error_t drqv_create(void **command_ref, session_t *session, request_t *request) {
    error_t err = ERROR_NONE;
    error_t out_error = ERROR_NONE;
    
    int64_t now = millis_since_reference(session);
    if (now < 0) {
        return ERROR_UNSPECIFIC;
    }
    
    channel_id_t channel_id = request->channel_id;
    
    command_drqv_t *command = zalloc(sizeof(command_drqv_t));
    if (!command) {
        return ERROR_MEMORY_ALLOCATION;
    }

    command->session = session;
    command->channel_id = channel_id;
    command->interval_not_before_timestamp = now;

    // TODO: check full format (atoi ignores alphabet)
    
    command->phase = atoi(request_get_option(request, "phase", "0"));
    if (command->phase != TASK_SCHEDULE_BEFORE_FLIGHT_MODEL && command->phase != TASK_SCHEDULE_AFTER_FLIGHT_MODEL) {
        error_channel(session, channel_id, -1, "unsupported phase");
        goto error;
    }

    command->times_remaining = INFINITE_REPETITION;
    if (request_has_option(request, "times")) {
        command->times_remaining = atoi(request_get_option(request, "times", "0"));
        if (command->times_remaining <= 0) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "number of times");
            goto error;
        }
    }

    char *req_freq = request_get_option(request, "freq", "1000ms");
    if (!is_valid_interval(req_freq)) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "invalid format for freq");
        goto error;
    }

    command->is_interval_frames = is_suffixed_frames(req_freq);
    command->interval = atoi(req_freq);
    if (command->interval <= 0) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "invalid value for freq");
        goto error;
    }

    command_parameter_t *parameter = request->parameters;
    if (!parameter) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "at least one dataref is required");
        goto error;
    }
    
    drqv_dataref_t **dataref_ref = &command->datarefs;
    while (parameter) {
        int offset_type_separator = strpos(parameter->parameter, ":", 0);
        if (offset_type_separator < 1) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "datarefs must be specified with type");
            goto error;
        }

        int name_length = strlen(parameter->parameter) - offset_type_separator - 1;
        if (name_length < 1) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "dataref name is missing");
            goto error;
        }

        XPLMDataTypeID wanted_type = parse_type(parameter->parameter, offset_type_separator);
        if (wanted_type == xplmType_Unknown) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unknown type");
            goto error;
        }

        drqv_dataref_t *dataref = zalloc(sizeof(drqv_dataref_t));
        if (!dataref) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "internal dataref could not be allocated");
            out_error = ERROR_MEMORY_ALLOCATION;
            goto error;
        }

        dataref->type = wanted_type;

        dataref->name = copy_string(&parameter->parameter[offset_type_separator+1]);
        if (!dataref->name) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "dataref name could not be copied");
            out_error = ERROR_MEMORY_ALLOCATION;
            goto error;
        }
        
        *dataref_ref = dataref;
        
        // preallocate if required buffer size is already known
        if (wanted_type == xplmType_Int || wanted_type == xplmType_Float || wanted_type == xplmType_Double) {
            dataref->value_buffer_size = (wanted_type == xplmType_Double) ? 8 : 4;
            dataref->value_buffer = zalloc(dataref->value_buffer_size);
            if (!dataref->value_buffer) {
                error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "internal dataref value buffer could not be allocated");
                out_error = ERROR_MEMORY_ALLOCATION;
                goto error;
            }
        }

        parameter = parameter->next;
    }

    task_t *task = zalloc(sizeof(task_t));
    if (!task) {
        out_error = ERROR_MEMORY_ALLOCATION;
        goto error;
    }
    task->on_processing = drqv_process;
    task->reference = command;

    err = lock_schedule(session->server->config.task_schedule);
    if (err == ERROR_NONE) {
        err = schedule_task(session->server->config.task_schedule, task, command->phase);
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

    *command_ref = command;
    
    return ERROR_NONE;

 error:
    if (command) {
        drqv_dataref_t *dataref = command->datarefs;
        while (dataref) {
            drqv_dataref_t *next_dataref = dataref->next;
            
            if (dataref->name) {
                free(dataref->name);
            }
            
            if (dataref->value_buffer) {
                free(dataref->value_buffer);
            }
            
            free(dataref);
            dataref = next_dataref;
        }
        
        free(command);
    }
    return (out_error != ERROR_NONE) ? out_error : ERROR_UNSPECIFIC;
}

command_t command_drqv = {
    .name = "DRQV",
    .create = drqv_create,
    .terminate = drqv_terminate,
    .destroy = drqv_destroy,
};
