#include "command_drqv.h"

#include <string.h>

#include <XPLMDataAccess.h>

#include "arrays.h"
#include "logger.h"
#include "protocol.h"
#include "session.h"
#include "utils.h"
#include "xptypes.h"

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
    int64_t timestamp; // last flightloop, 0 if data was not updated since it has last been sent
} command_drqv_t;

static const char *drqv_supported_options[] = {
    "freq",
    "phase",
    "times",
    NULL
};

static error_t drqv_destroy(void *command_ref) {
    RCLOG_TRACE("[DRQV] destroy");
    
    if (!command_ref) {
        return ERROR_UNSPECIFIC;
    }
    
    command_drqv_t *command = command_ref;
    
    drqv_dataref_t *dataref = command->datarefs;
    command->datarefs = NULL;
    while (dataref) {
        drqv_dataref_t *next_dataref = dataref->next;
        
        if (dataref->name) {
            RCLOG_TRACE("[DRQV] destroy: freeing dataref name");
            free(dataref->name);
        }

        if (dataref->value_buffer) {
            RCLOG_TRACE("[DRQV] destroy: freeing dataref buffer");
            free(dataref->value_buffer);
        }
        
        RCLOG_TRACE("[DRQV] destroy: freeing dataref");
        free(dataref);
        dataref = next_dataref;
    }
    
    RCLOG_TRACE("[DRQV] destroy: freeing command");
    free(command);
    
    RCLOG_TRACE("[DRQV] destroy: done");

    return ERROR_NONE;
}

static error_t drqv_terminate(void *command_ref) {
    RCLOG_TRACE("[DRQV] terminate");
    
    if (!command_ref) {
        return ERROR_UNSPECIFIC;
    }
    
    error_t err = ERROR_NONE;
    command_drqv_t *command = command_ref;

    // channel may have been closed before (by error or finishing); ignore error
    finish_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, NULL);

    if (command->task) {
        RCLOG_TRACE("[DRQV] terminate: have task, unscheduling");
        task_schedule_t *task_schedule = command->session->server->config.task_schedule;
        
        err = lock_schedule(task_schedule);
        if (err == ERROR_NONE) {
            err = unschedule_task(task_schedule, command->task, command->phase);
            unlock_schedule(task_schedule);
        }
        
        if (err != ERROR_NONE) {
            RCLOG_WARN("[DRQV] terminate failed to unschedule task: %d", err);
            return err;
        }
        
        RCLOG_TRACE("[DRQV] terminate: freeing task");
        free(command->task);
        command->task = NULL;
    }

    channel_id_t channel_id = command->channel_id;
    
    RCLOG_TRACE("[DRQV] terminate: poisoning channel ID");
    command->channel_id = BAD_CHANNEL_ID;
    
    request_channel_destruction(command->session->channels, channel_id);
    
    return ERROR_NONE;
}

static bool drqv_initialize(command_drqv_t *command) {
    drqv_dataref_t *dataref = command->datarefs;
    while (dataref) {
        if (!dataref->value_buffer) {
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "internal error preparing dataref buffer");
            command->failed = true;
            return false;
        }
        
        dataref->xp_ref = XPLMFindDataRef(dataref->name);
        if (!dataref->xp_ref) {
            RCLOG_DEBUG("[DRQV] XP did not find dataref: \"%s\"", dataref->name);
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "dataref does not exist");
            command->failed = true;
            return false;
        }

        XPLMDataTypeID available_types = XPLMGetDataRefTypes(dataref->xp_ref);
        if (!(available_types & dataref->type)) {
            RCLOG_DEBUG("[DRQV] wanted type %d, got types %d", dataref->type, available_types);
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "type is not available from dataref");
            command->failed = true;
            return false;
        }
        
        dataref = dataref->next;
    }
    
    command->initialized = true;
    
    return command->initialized;
}

typedef int (*xplm_dataref_array_getter_f)(XPLMDataRef, void*, int, int); // 2nd argument void* matches for blob, otherwise int* or float*

static bool copy_xplm_dataref_array(command_drqv_t *command, drqv_dataref_t *dataref, xplm_dataref_array_getter_f xplm_getter) {
    int length = xplm_getter(dataref->xp_ref, NULL, 0, 0);
    if (length < 0) {
        error_channel(command->session, command->channel_id, command->timestamp, "negative length received for dataref array");
        return false;
    }
    
    if (!dynamic_array_set_length(dataref->value_buffer, length)) {
        error_channel(command->session, command->channel_id, command->timestamp, "failed to resize internal array");
        return false;
    }
    
    void *dest = dynamic_array_get_pointer(dataref->value_buffer, 0);
    int actual = xplm_getter(dataref->xp_ref, dest, 0, length);
    if (actual != length) {
        error_channel(command->session, command->channel_id, command->timestamp, "dataref returned inconsistent number of items");
        return false;
    }

    return true;
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
            should_run = (command->interval_wait <= 0);
        }
    }

    // skip further updates if we should not run
    if (!should_run) {
        return;
    }

    // reset "timer"
    if (command->is_interval_frames) {
        // interval is frame-based: just reset number of frames
        command->interval_wait = command->interval;
    } else {
        // interval is time-based: forward to next multiple based on start time
        // (try to maintain interval, avoid progressively slower intervals)
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

        case xplmType_IntArray:
            if (!copy_xplm_dataref_array(command, dataref, (xplm_dataref_array_getter_f) XPLMGetDatavi)) {
                command->failed = true;
                return;
            }
            break;

        case xplmType_FloatArray:
            if (!copy_xplm_dataref_array(command, dataref, (xplm_dataref_array_getter_f) XPLMGetDatavf)) {
                command->failed = true;
                return;
            }
            break;

        case xplmType_Data:
            if (!copy_xplm_dataref_array(command, dataref, (xplm_dataref_array_getter_f) XPLMGetDatab)) {
                command->failed = true;
                return;
            }
            break;

        default:
            error_channel(command->session, command->channel_id, command->timestamp, "unsupported type");
            command->failed = true;
            return;
        }
        
        dataref = dataref->next;
    }
}

static char* encode_dataref_value(drqv_dataref_t *dataref) {
    if (dataref->type == xplmType_IntArray || dataref->type == xplmType_FloatArray || dataref->type == xplmType_Data) {
        return xprc_encode_array(dataref->type, dataref->value_buffer);
    } else {
        return xprc_encode_value(dataref->type, dataref->value_buffer, dataref->value_buffer_size);
    }
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
        if (is_first) {
            is_first = false;
        } else {
            char *separator_copy = copy_string(";");
            if (!separator_copy) {
                error_channel(command->session, command->channel_id, command->timestamp, "failed to copy separator");
                command->failed = true;
                destroy_preallocated_list(list, free, PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS);
                return;
            }

            total_length++;
            if (!prealloc_list_append(list, separator_copy)) {
                error_channel(command->session, command->channel_id, command->timestamp, "failed to append separator to list");
                command->failed = true;
                destroy_preallocated_list(list, free, PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS);
                return;
            }
        }
        
        char *encoded_value = encode_dataref_value(dataref);
        if (!encoded_value) {
            error_channel(command->session, command->channel_id, command->timestamp, "failed to encode value");
            command->failed = true;
            destroy_preallocated_list(list, free, PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS);
            return;
        }

        total_length += strlen(encoded_value);
        if (!prealloc_list_append(list, encoded_value)) {
            error_channel(command->session, command->channel_id, command->timestamp, "failed to append encoded value to list");
            command->failed = true;
            destroy_preallocated_list(list, free, PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS);
            return;
        }

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

    bool will_continue = (command->times_remaining > 0) || (command->times_remaining == INFINITE_REPETITION);
    error_t err = ERROR_NONE;
    if (will_continue) {
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

    if (!request_has_only_options(request, (char**)drqv_supported_options)) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unsupported options");
        return ERROR_UNSPECIFIC;
    }
    
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

        XPLMDataTypeID wanted_type = xprc_parse_type(parameter->parameter, offset_type_separator);
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

        // allocate buffers
        if (wanted_type == xplmType_Int || wanted_type == xplmType_Float || wanted_type == xplmType_Double) {
            // single value: exact size is already known by type
            dataref->value_buffer_size = (wanted_type == xplmType_Double) ? SIZE_XPLM_DOUBLE : SIZE_XPLM_INT_FLOAT;
            dataref->value_buffer = zalloc(dataref->value_buffer_size);
            if (!dataref->value_buffer) {
                error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "internal dataref direct value buffer could not be allocated");
                out_error = ERROR_MEMORY_ALLOCATION;
                goto error;
            }
        } else if (wanted_type == xplmType_IntArray || wanted_type == xplmType_FloatArray || wanted_type == xplmType_Data) {
            // array types: create only dynamic array, length and capacity are determined during data retrieval
            size_t item_size = (wanted_type == xplmType_Data) ? 1 : SIZE_XPLM_INT_FLOAT;
            dataref->value_buffer = create_dynamic_array(item_size, 0);
            if (!dataref->value_buffer) {
                error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "internal dataref dynamic array value buffer could not be allocated");
                out_error = ERROR_MEMORY_ALLOCATION;
                goto error;
            }
        } else {
            // unsupported type
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "dataref type is not handled");
            out_error = ERROR_UNSPECIFIC;
            goto error;
        }

        parameter = parameter->next;
        dataref_ref = &dataref->next;
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
        free(task);
        out_error = err;
        goto error;
    }

    *command_ref = command;
    
    return ERROR_NONE;

 error:
    drqv_destroy(command);
    return (out_error != ERROR_NONE) ? out_error : ERROR_UNSPECIFIC;
}

command_t command_drqv = {
    .name = "DRQV",
    .create = drqv_create,
    .terminate = drqv_terminate,
    .destroy = drqv_destroy,
};
