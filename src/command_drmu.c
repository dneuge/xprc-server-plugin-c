#include "command_drmu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <XPLMDataAccess.h>

#include "arrays.h"
#include "protocol.h"
#include "session.h"
#include "utils.h"
#include "xptypes.h"

#define DRMU_METHOD_UNSUPPORTED 0
#define DRMU_METHOD_IMMEDIATE 1
#define DRMU_METHOD_LINEAR 2
typedef uint8_t drmu_method_t;

#define DRMU_MONITOR_UNSUPPORTED 0
#define DRMU_MONITOR_NONE 1
#define DRMU_MONITOR_GET 2
#define DRMU_MONITOR_SET 3
typedef uint8_t drmu_monitor_mode_t;

#define DRMU_WILL_NOT_CONTINUE false

typedef struct _drmu_dataref_t drmu_dataref_t;
typedef struct _drmu_dataref_t {
    // base = start/original values to compute values to be set from
    dynamic_array_t *base_values;
    bool base_on_dataref;

    // target = requested (end) values to be set
    dynamic_array_t *target_values;
    int target_array_offset;

    // buffer = pre-allocated array holding values to be set per iteration
    // also used for monitoring values - unmodified if monitoring=set, will be
    // overridden by retrieval if monitoring=get
    dynamic_array_t *buffer_values;
    
    char *name;
    XPLMDataTypeID type;
    XPLMDataRef xp_ref;
    dataproxy_t *proxy;

    drmu_dataref_t *next;
} drmu_dataref_t;

typedef struct {
    session_t *session;
    channel_id_t channel_id;
    
    task_t *task;
    task_schedule_phase_t phase;

    drmu_method_t method;
    drmu_monitor_mode_t monitor_mode;

    int64_t base_time_millis;
    
    int32_t interval;
    int32_t interval_remaining_frames;
    bool is_interval_frames;
    
    int32_t duration;
    int32_t duration_remaining_frames;
    bool is_duration_frames;

    bool initialized;
    bool failed;
    bool done;

    drmu_dataref_t *datarefs;
    int64_t timestamp; // last flightloop, 0 if data was not updated since it has last been sent
} command_drmu_t;

static const XPLMDataTypeID simple_types = xplmType_Int | xplmType_Float | xplmType_Double;
static const XPLMDataTypeID array_types = xplmType_IntArray | xplmType_FloatArray | xplmType_Data;

static error_t drmu_destroy(void *command_ref) {
    if (!command_ref) {
        return ERROR_UNSPECIFIC;
    }
    
    command_drmu_t *command = command_ref;
    
    drmu_dataref_t *dataref = command->datarefs;
    command->datarefs = NULL;
    while (dataref) {
        drmu_dataref_t *next_dataref = dataref->next;

        if (dataref->base_values) {
            destroy_dynamic_array(dataref->base_values);
            dataref->base_values = NULL;
        }
        
        if (dataref->target_values) {
            destroy_dynamic_array(dataref->target_values);
            dataref->target_values = NULL;
        }
        
        if (dataref->buffer_values) {
            destroy_dynamic_array(dataref->buffer_values);
            dataref->buffer_values = NULL;
        }
        
        if (dataref->name) {
            free(dataref->name);
            dataref->name = NULL;
        }
        
        free(dataref);
        dataref = next_dataref;
    }
    
    free(command);
    
    return ERROR_NONE;
}

static error_t drmu_terminate(void *command_ref) {
    if (!command_ref) {
        return ERROR_UNSPECIFIC;
    }
    
    error_t err = ERROR_NONE;
    command_drmu_t *command = command_ref;

    // channel may have been closed before (by error or finishing); ignore error
    finish_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, NULL);

    if (command->task) {
        task_schedule_t *task_schedule = command->session->server->config.task_schedule;
        
        err = lock_schedule(task_schedule);
        if (err == ERROR_NONE) {
            err = unschedule_task(task_schedule, command->task, command->phase);
            unlock_schedule(task_schedule);
        }
        
        if (err != ERROR_NONE) {
            printf("[XPRC] [DRMU] terminate failed to unschedule task: %d\n", err);
            return err;
        }
        
        free(command->task);
        command->task = NULL;
    }

    // poison and destroy channel
    channel_id_t channel_id = command->channel_id;
    command->channel_id = BAD_CHANNEL_ID;
    request_channel_destruction(command->session->channels, channel_id);
    
    return ERROR_NONE;
}

static bool drmu_initialize(command_drmu_t *command) {
    // TODO: implement or remove
    /*
    drmu_dataref_t *dataref = command->datarefs;
    while (dataref) {
        if (!dataref->value_buffer) {
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "internal error preparing dataref buffer");
            command->failed = true;
            return false;
        }
        
        dataref->xp_ref = XPLMFindDataRef(dataref->name);
        if (!dataref->xp_ref) {
            printf("[XPRC] [DRMU] XP did not find dataref: \"%s\"\n", dataref->name); // DEBUG
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "dataref does not exist");
            command->failed = true;
            return false;
        }

        XPLMDataTypeID available_types = XPLMGetDataRefTypes(dataref->xp_ref);
        if (!(available_types & dataref->type)) {
            printf("[XPRC] [DRMU] wanted type %d, got types %d\n", dataref->type, available_types); // DEBUG
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "type is not available from dataref");
            command->failed = true;
            return false;
        }
        
        dataref = dataref->next;
    }
    */
    
    command->initialized = true;
    
    return command->initialized;
}

typedef int (*xplm_dataref_array_getter_f)(XPLMDataRef, void*, int, int); // 2nd argument void* matches for blob, otherwise int* or float*

/*
static bool copy_xplm_dataref_array(command_drmu_t *command, drmu_dataref_t *dataref, xplm_dataref_array_getter_f xplm_getter) {
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
*/

static void drmu_process_flightloop(command_drmu_t *command) {
    /*
    if (command->failed) {
        return;
    }

    int64_t now = millis_since_reference(command->session);
    if (now < 0) {
        return;
    }

    bool should_run = false;
    if (!command->initialized) {
        if (!drmu_initialize(command)) {
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
    drmu_dataref_t *dataref = command->datarefs;
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
    */
}

static char* encode_dataref_value(drmu_dataref_t *dataref) {
    bool is_array_type = ((array_types & dataref->type) != 0);
    if (is_array_type) {
        return xprc_encode_array(dataref->type, dataref->buffer_values);
    } else {
        void *value = dynamic_array_get_pointer(dataref->buffer_values, 0);
        if (!value) {
            return NULL;
        }
        return xprc_encode_value(dataref->type, value, dataref->buffer_values->item_size);
    }
}

static void drmu_process_post(command_drmu_t *command) {
    printf("[XPRC] [DRMU] post-processing\n"); // DEBUG

    drmu_terminate(command);
    
    /*
    if (command->failed) {
        drmu_terminate(command);
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
    */
}

static void submit_monitor_data(command_drmu_t *command, bool will_continue) {
    if (command->monitor_mode == DRMU_MONITOR_NONE) {
        return;
    }
    
    prealloc_list_t *list = create_preallocated_list();
    if (!list) {
        error_channel(command->session, command->channel_id, command->timestamp, "failed to allocate list for value encoding");
        command->failed = true;
        return;
    }

    drmu_dataref_t *dataref = command->datarefs;
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

    error_t err = ERROR_NONE;
    if (will_continue) {
        err = continue_channel(command->session, command->channel_id, command->timestamp, out);
    } else {
        err = finish_channel(command->session, command->channel_id, command->timestamp, out);
    }
    
    free(out);

    if (err != ERROR_NONE) {
        error_channel(command->session, command->channel_id, command->timestamp, "error on result submission");
        command->failed = true;
    }
    
    command->timestamp = 0;
}

static void drmu_process(task_t *task, task_schedule_phase_t phase) {
    command_drmu_t *command = task->reference;

    if (phase == command->phase) {
        drmu_process_flightloop(command);
    } else if (phase == TASK_SCHEDULE_POST_PROCESSING) {
        drmu_process_post(command);
    }
}

static error_t complete_without_schedule(command_drmu_t *command) {
    // all datarefs are XPRC-internal and can be manipulated in a single run without
    // any timing behaviour; types have also been checked before

    error_t err = ERROR_NONE;
    bool locked = false;
    
    err = lock_dataproxy_registry(command->session->server->config.dataproxy_registry);
    if (err != ERROR_NONE) {
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "XPRC-internal: failed to lock dataproxy registry");
        goto error;
    }
    locked = true;

    command->timestamp = millis_since_reference(command->session);
    if (command->timestamp < 0) {
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "failed to get session timestamp");
        goto error;
    }

    bool success = true;
    drmu_dataref_t *dataref = command->datarefs;
    while (dataref) {
        bool is_simple_type = ((simple_types & dataref->type) != 0);
        
        if (!dataref->proxy) {
            goto error;
        }
        
        if (!is_simple_type && !dynamic_array_set_length(dataref->buffer_values, dataref->target_values->length)) {
            error_channel(command->session, command->channel_id, command->timestamp, "XPRC-internal: failed to reset buffer array size");
            goto error;
        }

        void *src = dynamic_array_get_pointer(dataref->target_values, 0);
        if (!src) {
            goto error;
        }

        if (is_simple_type) {
            err = dataproxy_simple_set(dataref->proxy, dataref->type, src, command->session);
        } else {
            err = dataproxy_array_update(dataref->proxy, dataref->type, src, dataref->target_array_offset, dataref->target_values->length, command->session);
        }

        if (err != ERROR_NONE) {
            // in case setting failed we still want to continue with the remaining datarefs
            success = false;
            printf("[XPRC] [DRMU] complete_without_schedule setting value failed (type %d, offset %d, error %d): %s\n", dataref->type, dataref->target_array_offset, err, dataref->name);
        }
        
        if (success) {
            if (command->monitor_mode == DRMU_MONITOR_SET) {
                // on error still continue trying to set other datarefs; monitor will be skipped
                success = dynamic_array_copy_from_other(
                                                        dataref->buffer_values, 0,
                                                        dataref->target_values, 0,
                                                        DYNAMIC_ARRAY_COPY_ALL,
                                                        DYNAMIC_ARRAY_DENY_CAPACITY_CHANGE,
                                                        DYNAMIC_ARRAY_ALLOW_LENGTH_CHANGE
                                                       );
                if (!success) {
                    printf("[XPRC] [DRMU] complete_without_schedule failed to copy target to monitor data\n");
                }
            } else if (command->monitor_mode == DRMU_MONITOR_GET) {
                void *dest = dynamic_array_get_pointer(dataref->buffer_values, 0);
                if (!dest) {
                    goto error;
                }
            
                if (is_simple_type) {
                    err = dataproxy_simple_get(dataref->proxy, dataref->type, dest);
                } else {
                    int num_copied = 0;
                    err = dataproxy_array_get(dataref->proxy, dataref->type, dest, &num_copied, dataref->target_array_offset, dataref->target_values->length);
                
                    if (num_copied < dataref->buffer_values->length) {
                        if (!dynamic_array_set_length(dataref->buffer_values, num_copied)) {
                            // still continue trying to set other datarefs; monitor will be skipped
                            success = false;
                            printf("[XPRC] [DRMU] complete_without_schedule failed to reduce monitor array size to %d (target length: %d)\n", num_copied, dataref->target_values->length);
                        }
                    }
                }
                
                if (err != ERROR_NONE) {
                    // still continue trying to set other datarefs; monitor will be skipped
                    success = false;
                    printf("[XPRC] [DRMU] complete_without_schedule failed to get monitor data (type %d, offset %d, error %d): %s\n", dataref->type, dataref->target_array_offset, err, dataref->name);
                }
            }
        }

        dataref = dataref->next;
    }

    if (!success) {
        goto error;
    }
    
    unlock_dataproxy_registry(command->session->server->config.dataproxy_registry);
    locked = false;

    submit_monitor_data(command, DRMU_WILL_NOT_CONTINUE);
    
    goto terminate;

 error:
    error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, NULL);
    goto terminate;
 
 terminate:
    if (locked) {
        unlock_dataproxy_registry(command->session->server->config.dataproxy_registry);
    }
    
    return drmu_terminate(command);
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

static drmu_method_t parse_method(char *s) {
    int len = strlen(s);
    if ((len == 9) && !strcmp(s, "immediate")) {
        return DRMU_METHOD_IMMEDIATE;
    } else if ((len == 6) && !strcmp(s, "linear")) {
        return DRMU_METHOD_LINEAR;
    } else {
        return DRMU_METHOD_UNSUPPORTED;
    }
}

static drmu_monitor_mode_t parse_monitor_mode(char *s) {
    int len = strlen(s);
    if ((len == 4) && !strcmp(s, "none")) {
        return DRMU_MONITOR_NONE;
    } else if ((len == 3) && !strcmp(s, "set")) {
        return DRMU_MONITOR_SET;
    } else if ((len == 3) && !strcmp(s, "get")) {
        return DRMU_MONITOR_GET;
    } else {
        return DRMU_MONITOR_UNSUPPORTED;
    }
}

/*
static void debug_dump(char *prefix, XPLMDataTypeID type, dynamic_array_t *arr) {
    if (!arr) {
        printf("%s is null\n", prefix);
        return;
    }

    printf("%s has length %d\n", prefix, arr->length);
    if (arr->length <= 0) {
        return;
    }

    if (type == xplmType_Int) {
        type = xplmType_IntArray;
    } else if (type == xplmType_Float) {
        type = xplmType_FloatArray;
    }

    if ((type & array_types) != 0) {
        char *s = xprc_encode_array(type, arr);
        if (!s) {
            printf("%s encoding failed\n", prefix);
            return;
        }

        printf("%s: %s\n", prefix, s);
        free(s);
        return;
    }
    
    if (type != xplmType_Double) {
        printf("%s has unhandled type for debug output: %d\n", prefix, type);
        return;
    }

    for (int i=0; i<arr->length; i++) {
        printf("%s[%d] = %f\n", prefix, i, dynamic_array_get_item(xpdouble_t, arr, i));
    }
}
*/

static error_t drmu_create(void **command_ref, session_t *session, request_t *request) {
    error_t err = ERROR_NONE;
    error_t out_error = ERROR_NONE;
    
    int64_t now = millis_since_reference(session);
    if (now < 0) {
        return ERROR_UNSPECIFIC;
    }
    
    channel_id_t channel_id = request->channel_id;
    
    command_drmu_t *command = zalloc(sizeof(command_drmu_t));
    if (!command) {
        return ERROR_MEMORY_ALLOCATION;
    }

    command->session = session;
    command->channel_id = channel_id;
    command->base_time_millis = now;

    // TODO: check full format (atoi ignores alphabet)
    
    command->phase = atoi(request_get_option(request, "phase", "0"));
    if (command->phase != TASK_SCHEDULE_BEFORE_FLIGHT_MODEL && command->phase != TASK_SCHEDULE_AFTER_FLIGHT_MODEL) {
        error_channel(session, channel_id, -1, "unsupported phase");
        goto error;
    }

    char *req_freq = request_get_option(request, "repeatFreq", "0f");
    if (!is_valid_interval(req_freq)) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "invalid format for repeatFreq");
        goto error;
    }

    command->is_interval_frames = is_suffixed_frames(req_freq);
    command->interval = atoi(req_freq);
    if (command->interval < 0) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "invalid value for repeatFreq");
        goto error;
    }

    char *req_duration = request_get_option(request, "duration", "2500ms");
    if (!is_valid_interval(req_duration)) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "invalid format for duration");
        goto error;
    }

    command->is_duration_frames = is_suffixed_frames(req_duration);
    command->duration = atoi(req_duration);
    if (command->duration <= 0) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "invalid value for duration");
        goto error;
    }

    command->method = parse_method(request_get_option(request, "method", "immediate"));
    if (command->method == DRMU_METHOD_UNSUPPORTED) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unsupported method");
        goto error;
    }

    command->monitor_mode = parse_monitor_mode(request_get_option(request, "monitor", "none"));
    if (command->monitor_mode == DRMU_MONITOR_UNSUPPORTED) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unsupported monitor mode");
        goto error;
    }

    //printf("[XPRC] [DRMU] create: phase=%d, interval=%d (frames? %d), duration=%d (frames? %d), method=%d, monitor=%d\n", command->phase, command->interval, command->is_interval_frames, command->duration, command->is_duration_frames, command->method, command->monitor_mode); // DEBUG

    command_parameter_t *parameter = request->parameters;
    if (!parameter) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "at least one dataref is required");
        goto error;
    }

    bool has_only_internal_datarefs = true;
    drmu_dataref_t **dataref_ref = &command->datarefs;
    while (parameter) {
        int offset_type_separator = strpos(parameter->parameter, ":", 0);
        if (offset_type_separator < 1) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "datarefs must be specified with type");
            goto error;
        }

        int offset_name_separator = strpos_unescaped(parameter->parameter, ":", offset_type_separator + 1);
        if (offset_name_separator < 0) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "incomplete dataref specification");
            goto error;
        }
        
        int offset_index_separator = strpos_unescaped(parameter->parameter, "[", offset_type_separator + 1);
        bool has_array_index = (offset_index_separator >= 0) && (offset_index_separator < offset_name_separator);
        int name_length_escaped = (has_array_index ? offset_index_separator : offset_name_separator) - offset_type_separator - 1;
        if (name_length_escaped < 1) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "dataref name is missing");
            goto error;
        }
        
        int name_length = strlen(parameter->parameter) - offset_type_separator - 1;
        if (name_length < 1) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "dataref name is missing");
            goto error;
        }

        int array_offset = -1;
        if (has_array_index) {
            int offset_index_close = strpos(parameter->parameter, "]", offset_index_separator + 1);
            if (offset_index_close < 0 || offset_index_close >= offset_name_separator) {
                error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "dataref array offset not closed");
                goto error;
            }
            
            if (offset_index_close < offset_name_separator - 1) {
                error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "trailing input after array offset");
                goto error;
            }

            int index_length = offset_index_close - offset_index_separator - 1;
            if (index_length < 1) {
                error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "dataref array offset is empty");
                goto error;
            }

            char *tmp = copy_partial_string(&(parameter->parameter[offset_index_separator + 1]), index_length);
            if (!tmp) {
                error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "dataref array offset failed to parse");
                goto error;
            }
            array_offset = atoi(tmp);
            free(tmp);

            if (array_offset < 0) {
                error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "invalid dataref array offset");
                goto error;
            }
        }

        XPLMDataTypeID wanted_type = xprc_parse_type(parameter->parameter, offset_type_separator);
        if (wanted_type == xplmType_Unknown) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unknown type");
            goto error;
        }

        bool is_array_type = ((array_types & wanted_type) != 0);
        if (is_array_type && (array_offset < 0)) {
            // by default, array types always write to 0 offset
            array_offset = 0;
        }

        if (!is_array_type && (array_offset >= 0)) {
            // array values are needed for array access; reasons:
            // - type may be incompatible (e.g. double is not possible and blob wants raw bytes)
            // - retrieval (monitor=get) may end with zero length return value if out of bounds,
            //   there is no way to give any reasonable response on protocol unless array types are used
            //   (will have zero length)
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "array offset is only permitted for array types");
            goto error;
        }

        size_t type_size = SIZE_XPLM_INT_FLOAT;
        if (wanted_type == xplmType_Double) {
            type_size = SIZE_XPLM_DOUBLE;
        } else if (wanted_type == xplmType_Data) {
            type_size = 1;
        }

        int offset_set_separator = strpos_unescaped(parameter->parameter, ":", offset_name_separator + 1);
        bool defines_base_values = (offset_set_separator >= 0);
        int set_length = (defines_base_values ? offset_set_separator : strlen(parameter->parameter)) - offset_name_separator - 1;
        if (set_length < 1) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "missing target values to be set");
            goto error;
        }

        int base_length = defines_base_values ? (strlen(parameter->parameter) - offset_set_separator - 1) : 0;
        if (defines_base_values && (base_length < 1)) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "base values indicated but not provided");
            goto error;
        }
        
        drmu_dataref_t *dataref = zalloc(sizeof(drmu_dataref_t));
        if (!dataref) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "internal dataref could not be allocated");
            out_error = ERROR_MEMORY_ALLOCATION;
            goto error;
        }

        dataref->type = wanted_type;
        dataref->target_array_offset = array_offset;
        
        dataref->name = copy_partial_unescaped_string(&(parameter->parameter[offset_type_separator + 1]), name_length_escaped);
        if (!dataref->name) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "dataref name could not be copied");
            out_error = ERROR_MEMORY_ALLOCATION;
            goto error;
        }
        
        //printf("[XPRC] [DRMU] create, dataref: name=\"%s\", type=%d, target_array_offset=%d\n", dataref->name, dataref->type, dataref->target_array_offset); // DEBUG

        *dataref_ref = dataref;

        if ((wanted_type & array_types) != 0) {
            dataref->target_values = xprc_parse_array(&(parameter->parameter[offset_name_separator+1]), set_length, wanted_type);
        } else {
            dataref->target_values = create_dynamic_array(type_size, 1);
            if (dataref->target_values && (!dynamic_array_set_length(dataref->target_values, 1) || !xprc_parse_value(&(parameter->parameter[offset_name_separator+1]), set_length, wanted_type, dynamic_array_get_pointer(dataref->target_values, 0), type_size))) {
                destroy_dynamic_array(dataref->target_values);
                dataref->target_values = NULL;
            }
        }

        if (!dataref->target_values) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "failed to parse target values");
            goto error;
        }

        if (dataref->target_values->length < 1) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "empty array of target values");
            goto error;
        }

        //debug_dump("[XPRC] [DRMU] create, dataref target_values", dataref->type, dataref->target_values); // DEBUG

        dataref->buffer_values = create_dynamic_array(type_size, dataref->target_values->length);
        if (!dataref->buffer_values || !dynamic_array_set_length(dataref->buffer_values, dataref->target_values->length)) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "failed to allocate buffer values");
            out_error = ERROR_MEMORY_ALLOCATION;
            goto error;
        }

        if (defines_base_values) {
            if ((wanted_type & array_types) != 0) {
                dataref->base_values = xprc_parse_array(&(parameter->parameter[offset_set_separator+1]), base_length, wanted_type);
            } else {
                dataref->base_values = create_dynamic_array(type_size, 1);
                if (dataref->base_values && (!dynamic_array_set_length(dataref->base_values, 1) || !xprc_parse_value(&(parameter->parameter[offset_set_separator+1]), base_length, wanted_type, dynamic_array_get_pointer(dataref->base_values, 0), type_size))) {
                    destroy_dynamic_array(dataref->base_values);
                    dataref->base_values = NULL;
                }
            }

            if (!dataref->base_values) {
                error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "failed to parse base values");
                goto error;
            }
            
            if (dataref->base_values->length != dataref->target_values->length) {
                error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "number of base and target values does not match");
                goto error;
            }
        }

        //debug_dump("[XPRC] [DRMU] create, dataref base_values", dataref->type, dataref->base_values); // DEBUG

        dataref->proxy = find_registered_dataproxy(session->server->config.dataproxy_registry, dataref->name);
        if (dataref->proxy) {
            if (!dataproxy_can_write(dataref->proxy, session)) {
                error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "XPRC-internal dataref is write-protected");
                goto error;
            }

            XPLMDataTypeID available_types = xplmType_Unknown;
            if (dataproxy_get_types(dataref->proxy, &available_types) != ERROR_NONE) {
                error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "XPRC-internal dataref failed to indicate available types");
                goto error;
            }

            if ((available_types & dataref->type) == 0) {
                error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "XPRC-internal dataref does not support requested type");
                goto error;
            }
        }
        has_only_internal_datarefs &= (dataref->proxy != NULL);

        //printf("[XPRC] [DRMU] create, dataref proxy=%p\n", dataref->proxy); // DEBUG

        parameter = parameter->next;
        dataref_ref = &dataref->next;
    }

    *command_ref = command;

    bool needs_timing = (command->method != DRMU_METHOD_IMMEDIATE) || (command->interval != 0);
    bool needs_scheduling = needs_timing || !has_only_internal_datarefs;
    //printf("[XPRC] [DRMU] create needs_timing=%d, needs_scheduling=%d\n", needs_timing, needs_scheduling); // DEBUG
    if (!needs_scheduling) {
        return complete_without_schedule(command);
    }

    task_t *task = zalloc(sizeof(task_t));
    if (!task) {
        out_error = ERROR_MEMORY_ALLOCATION;
        goto error;
    }
    task->on_processing = drmu_process;
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

    return ERROR_NONE;

 error:
    drmu_destroy(command);
    return (out_error != ERROR_NONE) ? out_error : ERROR_UNSPECIFIC;
}

command_t command_drmu = {
    .name = "DRMU",
    .create = drmu_create,
    .terminate = drmu_terminate,
    .destroy = drmu_destroy,
};
