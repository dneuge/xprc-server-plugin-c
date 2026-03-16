#include "command_drmu.h"

#include <math.h>
#include <string.h>

#include <XPLMDataAccess.h>

#include "arrays.h"
#include "logger.h"
#include "protocol.h"
#include "session.h"
#include "utils.h"
#include "xptypes.h"

#define DRMU_COMMAND_VERSION 1

#define DRMU_METHOD_UNSUPPORTED 0
#define DRMU_METHOD_IMMEDIATE 1
#define DRMU_METHOD_LINEAR 2
typedef uint8_t drmu_method_t;

#define DRMU_MONITOR_UNSUPPORTED 0
#define DRMU_MONITOR_NONE 1
#define DRMU_MONITOR_GET 2
#define DRMU_MONITOR_SET 3
typedef uint8_t drmu_monitor_mode_t;

#define DRMU_WILL_CONTINUE true
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
    int32_t duration_frames;
    bool is_duration_frames;

    bool initialized;
    bool failed;
    bool update_base_values;
    bool cycle_complete;
    bool done;
    
    drmu_dataref_t *datarefs;
    bool has_proxied_datarefs;
    bool has_fetch_based_datarefs;
    int64_t timestamp; // last flightloop, 0 if data was not updated since it has last been sent
} command_drmu_t;

static const XPLMDataTypeID simple_types = xplmType_Int | xplmType_Float | xplmType_Double;
static const XPLMDataTypeID array_types = xplmType_IntArray | xplmType_FloatArray | xplmType_Data;

static const char *drmu_supported_options[] = {
    "duration",
    "method",
    "methodFreq",
    "monitor",
    "phase",
    "repeatFreq",
    NULL
};

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
            RCLOG_WARN("[DRMU] terminate failed to unschedule task: %d", err);
            return err;
        }
        
        // just drop the reference but don't free the task; memory management is taken care of by schedule maintenance
        command->task = NULL;
    }

    // poison and destroy channel
    channel_id_t channel_id = command->channel_id;
    command->channel_id = BAD_CHANNEL_ID;
    request_channel_destruction(command->session->channels, channel_id);
    
    return ERROR_NONE;
}

static bool drmu_initialize(command_drmu_t *command) {
    // Scheduled DRMU commands may continue to run after internal datarefs
    // have already been unregistered from X-Plane and before they get re-registered,
    // so we need to retrieve the XP dataref as fallback for those as well.
    
    drmu_dataref_t *dataref = command->datarefs;
    while (dataref) {
        dataref->xp_ref = XPLMFindDataRef(dataref->name);
        if (!dataref->xp_ref) {
            RCLOG_DEBUG("[DRMU] XP did not find dataref: \"%s\"", dataref->name);
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "dataref does not exist");
            command->failed = true;
            return false;
        }

        XPLMDataTypeID available_types = XPLMGetDataRefTypes(dataref->xp_ref);
        if (!(available_types & dataref->type)) {
            RCLOG_DEBUG("[DRMU] wanted type %d, got types %d for dataref %s", dataref->type, available_types, dataref->name);
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

typedef void (*xplm_dataref_array_setter_f)(XPLMDataRef, void*, int, int); // 2nd argument void* matches for blob, otherwise int* or float*

static bool read_xplm_dataref_array(drmu_dataref_t *dataref, dynamic_array_t *dest_arr, xplm_dataref_array_getter_f xplm_getter) {
    void *dest = dynamic_array_get_pointer(dest_arr, 0);
    if (!dest) {
        return false;
    }
    
    int length = xplm_getter(dataref->xp_ref, dest, dataref->target_array_offset, dest_arr->capacity);
    
    return dynamic_array_set_length(dest_arr, length);
}

static bool write_xplm_dataref_array(drmu_dataref_t *dataref, dynamic_array_t *src_arr, xplm_dataref_array_setter_f xplm_setter) {
    void *src = dynamic_array_get_pointer(src_arr, 0);
    if (!src) {
        return false;
    }

    xplm_setter(dataref->xp_ref, src, dataref->target_array_offset, src_arr->length);
    
    return true;
}

static bool read_dataref(drmu_dataref_t *dataref, dynamic_array_t *dest_arr) {
    bool success = true;
    error_t err = ERROR_NONE;

    RCLOG_TRACE("[DRMU] read_dataref dataref=%p, type=%d, dest_arr=%p", dataref, dataref->type, dest_arr);

    bool is_simple_type = ((simple_types & dataref->type) != 0);
    
    void *dest = dynamic_array_get_pointer(dest_arr, 0);
    if (!dest) {
        RCLOG_TRACE("[DRMU] read_dataref no dest");
        return false;
    }
    
    RCLOG_TRACE("[DRMU] read_dataref dest_arr[length=%d, capacity=%d, item_size=%ld]", dest_arr->length, dest_arr->capacity, dest_arr->item_size);

    // retrieve internally if available
    if (dataref->proxy) {
        RCLOG_TRACE("[DRMU] read_dataref read via proxy");
        if (is_simple_type) {
            RCLOG_TRACE("[DRMU] read_dataref simple via proxy");
            err = dataproxy_simple_get(dataref->proxy, dataref->type, dest);
        } else {
            RCLOG_TRACE("[DRMU] read_dataref array via proxy");
            int num_copied = 0;
            err = dataproxy_array_get(dataref->proxy, dataref->type, dest, &num_copied, dataref->target_array_offset, dest_arr->length);
            if ((err == ERROR_NONE) && (num_copied != dest_arr->length)) {
                RCLOG_TRACE("[DRMU] read_dataref setting length");
                if (!dynamic_array_set_length(dest_arr, num_copied)) {
                    // this error cannot be recovered from by retrying through X-Plane
                    RCLOG_WARN("[DRMU] read_dataref failed to change array size to %d (dest_arr->length: %d)", num_copied, dest_arr->length);
                    return false;
                }
            }
        }
        RCLOG_TRACE("[DRMU] read_dataref retrieved via proxy");

        if (err == ERROR_NONE) {
            return true;
        }

        // in case we failed to retrieve the value directly from the proxy we will retry through X-Plane
        RCLOG_DEBUG("[DRMU] read_dataref fallback to X-Plane API for %s (proxy error: %d)", dataref->name, err);

        return false; // DEBUG
    }

    // FIXME: accessing a proxied dataref through X-Plane may deadlock

    // retrieve via X-Plane API
    RCLOG_TRACE("[DRMU] read_dataref read via X-Plane");
    if (dataref->type == xplmType_Int) {
        *((xpint_t*) dest) = XPLMGetDatai(dataref->xp_ref);
    } else if (dataref->type == xplmType_Float) {
        *((xpfloat_t*) dest) = XPLMGetDataf(dataref->xp_ref);
    } else if (dataref->type == xplmType_Double) {
        *((xpdouble_t*) dest) = XPLMGetDatad(dataref->xp_ref);
    } else if (dataref->type == xplmType_IntArray) {
        success = read_xplm_dataref_array(dataref, dest_arr, (xplm_dataref_array_getter_f) XPLMGetDatavi);
    } else if (dataref->type == xplmType_FloatArray) {
        success = read_xplm_dataref_array(dataref, dest_arr, (xplm_dataref_array_getter_f) XPLMGetDatavf);
    } else if (dataref->type == xplmType_Data) {
        success = read_xplm_dataref_array(dataref, dest_arr, (xplm_dataref_array_getter_f) XPLMGetDatab);
    } else {
        RCLOG_WARN("[DRMU] read_dataref called with unsupported type %d", dataref->type);
        return false;
    }

    RCLOG_TRACE("[DRMU] read_dataref done");
    return success;
}

static bool write_dataref(command_drmu_t *command, drmu_dataref_t *dataref, dynamic_array_t *values) {
    bool success = true;
    error_t err = ERROR_NONE;
    bool is_simple_type = ((simple_types & dataref->type) != 0);
    
    void *src = dynamic_array_get_pointer(values, 0);
    if (!src) {
        return false;
    }

    // set internally if available
    if (dataref->proxy) {
        if (is_simple_type) {
            err = dataproxy_simple_set(dataref->proxy, dataref->type, src, command->session);
        } else {
            err = dataproxy_array_update(dataref->proxy, dataref->type, src, dataref->target_array_offset, values->length, command->session);
        }

        if (err == ERROR_NONE) {
            return true;
        }

        // in case we failed to write the value directly to the proxy we will retry through X-Plane
        RCLOG_DEBUG("[DRMU] write_dataref fallback to X-Plane API for %s (proxy error: %d)", dataref->name, err);

        return false; // DEBUG
    }
    
    // FIXME: accessing a proxied dataref through X-Plane may deadlock

    // write to X-Plane API
    if (dataref->type == xplmType_Int) {
        XPLMSetDatai(dataref->xp_ref, *((xpint_t*) src));
    } else if (dataref->type == xplmType_Float) {
        XPLMSetDataf(dataref->xp_ref, *((xpfloat_t*) src));
    } else if (dataref->type == xplmType_Double) {
        XPLMSetDatad(dataref->xp_ref, *((xpdouble_t*) src));
    } else if (dataref->type == xplmType_IntArray) {
        success = write_xplm_dataref_array(dataref, values, (xplm_dataref_array_setter_f) XPLMSetDatavi);
    } else if (dataref->type == xplmType_FloatArray) {
        success = write_xplm_dataref_array(dataref, values, (xplm_dataref_array_setter_f) XPLMSetDatavf);
    } else if (dataref->type == xplmType_Data) {
        success = write_xplm_dataref_array(dataref, values, (xplm_dataref_array_setter_f) XPLMSetDatab);
    } else {
        RCLOG_WARN("[DRMU] write_dataref called with unsupported type %d", dataref->type);
        return false;
    }

    return success;
}

static double get_dataref_arr_value_as_double(drmu_dataref_t *dataref, dynamic_array_t *arr, int i) {
    void *data = dynamic_array_get_pointer(arr, i);
    if (!data) {
        return nan("");
    }

    if ((dataref->type == xplmType_Int) || (dataref->type == xplmType_IntArray)) {
        return *((xpint_t*) data);
    } else if ((dataref->type == xplmType_Float) || (dataref->type == xplmType_FloatArray)) {
        return *((xpfloat_t*) data);
    } else if (dataref->type == xplmType_Double) {
        return *((xpdouble_t*) data);
    } else {
        return nan("");
    }
}

static bool set_dataref_arr_value_from_double(drmu_dataref_t *dataref, dynamic_array_t *arr, int i, double value) {
    if (isnan(value)) {
        return false;
    }
    
    void *data = dynamic_array_get_pointer(arr, i);
    if (!data) {
        return false;
    }

    if ((dataref->type == xplmType_Int) || (dataref->type == xplmType_IntArray)) {
        *((xpint_t*) data) = (xpint_t) lrint(value);
    } else if ((dataref->type == xplmType_Float) || (dataref->type == xplmType_FloatArray)) {
        *((xpfloat_t*) data) = value;
    } else if (dataref->type == xplmType_Double) {
        *((xpdouble_t*) data) = value;
    } else {
        return false;
    }

    return true;
}

static bool calculate_linear_values(drmu_dataref_t *dataref, double progress) {
    if (progress < 0.0) {
        progress = 0.0;
    }
    
    for (int i=0; i < dataref->buffer_values->length; i++) {
        double base = get_dataref_arr_value_as_double(dataref, dataref->base_values, i);
        double target = get_dataref_arr_value_as_double(dataref, dataref->target_values, i);
        double diff = target - base;
        double value = base + (progress * diff);

        if (!set_dataref_arr_value_from_double(dataref, dataref->buffer_values, i, value)) {
            return false;
        }
    }

    return true;
}

static void drmu_process_flightloop(command_drmu_t *command) {
    error_t err = ERROR_NONE;
    bool locked_proxy_registry = false;
    
    if (command->failed || command->done) {
        return;
    }

    bool should_run = true; // TODO: default should be false if we actually decide that, see below
    if (!command->initialized) {
        if (!drmu_initialize(command)) {
            goto error;
        }
        confirm_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, NULL);

        // we always run immediately after initialization
        should_run = true;

        // base values always need to be updated on init unless all were provided
        command->update_base_values = command->has_fetch_based_datarefs;
    }

    // TODO: should_run would be determined through methodFreq
    /* else {
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
    */

    // TODO: avoid calculating timestamp while command->cycle_complete for frame-based interval
    
    int64_t now = millis_since_reference(command->session);
    if (now < 0) {
        goto error;
    }
    
    int64_t millis_since_base = now - command->base_time_millis;
    if (millis_since_base < 0) {
        // TODO: we may want to skip this iteration?
        RCLOG_WARN("[DRMU] time has moved backwards? base_time_millis=%ld, now=%ld, diff=%ld", command->base_time_millis, now, millis_since_base);
        millis_since_base = 0;
    }

    // check if next cycle is due
    bool restart = false;
    if (command->interval > 0) {
        if (command->is_interval_frames) {
            command->interval_remaining_frames--;
            if (command->interval_remaining_frames <= 0) {
                command->interval_remaining_frames = command->interval;
                command->base_time_millis = now;
                restart = true;
            }
        } else {
            while (millis_since_base >= command->interval) {
                command->base_time_millis += command->interval;
                millis_since_base = now - command->base_time_millis;
                restart = true;
            }
        }
    }

    if (command->cycle_complete && !restart) {
        goto exit;
    }

    if (restart) {
        RCLOG_TRACE("[DRMU] restart");
        command->cycle_complete = false;
        command->duration_frames = 0; // TODO: 0 or -1? will increment below
        command->update_base_values = command->has_fetch_based_datarefs;

        // interval can be frame-based while duration is time-based, so we need to
        // recalculate the time again
        millis_since_base = now - command->base_time_millis;
        if (millis_since_base < 0) {
            // this *really* should not happen
            RCLOG_ERROR("[DRMU] negative millis_since_base while resetting: interval=%d, base_time_millis=%ld, now=%ld, millis_since_base=%ld", command->interval, command->base_time_millis, now, millis_since_base);
            millis_since_base = 0;
        }
    }
    
    double progress = 1.0;
    if (command->method != DRMU_METHOD_IMMEDIATE) {
        if (!command->is_duration_frames) {
            progress = ((double) millis_since_base) / ((double) command->duration);
        } else {
            // TODO: duration_frames still needs to be updated if should_run/methodFreq is supported
            command->duration_frames++;
            progress = ((double) command->duration_frames) / ((double) command->duration);
        }

        if (progress < 0.0) {
            // this shouldn't really be possible but it's better to protect from that case nevertheless
            RCLOG_WARN("[DRMU] limiting progress=%f to zero", progress);
            progress = 0.0;
        }
    }
    
    if (command->has_proxied_datarefs) {
        RCLOG_TRACE("[DRMU] locking dataproxy registry");
        err = lock_dataproxy_registry(command->session->server->config.dataproxy_registry);
        if (err != ERROR_NONE) {
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "failed to lock dataproxy registry");
            goto error;
        }
        locked_proxy_registry = true;
        RCLOG_TRACE("[DRMU] locked dataproxy registry");
    }

    // update data
    command->timestamp = now;
    drmu_dataref_t *dataref = command->datarefs;
    bool all_success = true; // continue with all datarefs before stopping after an error occurred
    while (dataref) {
        bool success = true;

        RCLOG_TRACE("[DRMU] dataref name=\"%s\", proxy=%p, xp_ref=%p", dataref->name, dataref->proxy, dataref->xp_ref);
        
        if (success && command->update_base_values && dataref->base_on_dataref) {
            RCLOG_TRACE("[DRMU] updating base values");

            if (success && !read_dataref(dataref, dataref->base_values)) {
                RCLOG_WARN("[DRMU] failed to update base values for %s (type %d, offset %d)", dataref->name, dataref->type, dataref->target_array_offset);
                success = false;
            }

            if (success && (dataref->base_values->length != dataref->target_values->length)) {
                RCLOG_WARN("[DRMU] base values length mismatch for %s (type %d, offset %d): expected %d, got %d", dataref->name, dataref->type, dataref->target_array_offset, dataref->target_values->length, dataref->base_values->length);
                success = false;
            }
            
            RCLOG_TRACE("[DRMU] base values updated");
        }

        dynamic_array_t *submit_values = dataref->buffer_values;
        bool monitor_copy_success = true;
        if (success) {
            if ((command->method == DRMU_METHOD_IMMEDIATE) || (progress >= 1.0)) {
                // we are done and can simply submit the target values
                submit_values = dataref->target_values;
                command->cycle_complete = true;
                command->done = (command->interval <= 0);

                RCLOG_DEBUG("[DRMU] immediate or done, progress=%f", progress);
                
                if (command->monitor_mode != DRMU_MONITOR_NONE) {
                    // monitor values will still be read from buffer_values so we need to copy them
                    monitor_copy_success = dynamic_array_copy_from_other(
                                                                         dataref->buffer_values, 0,
                                                                         dataref->target_values, 0,
                                                                         DYNAMIC_ARRAY_COPY_ALL,
                                                                         DYNAMIC_ARRAY_DENY_CAPACITY_CHANGE,
                                                                         DYNAMIC_ARRAY_ALLOW_LENGTH_CHANGE
                                                                        );
                    if (!monitor_copy_success) {
                        RCLOG_WARN("[DRMU] failed to copy target to monitor data");
                    }
                }
            } else if (command->method == DRMU_METHOD_LINEAR) {
                RCLOG_DEBUG("[DRMU] linear, progress=%f", progress);
                if (!calculate_linear_values(dataref, progress)) {
                    RCLOG_WARN("[DRMU] failed to calculate linear values: progress=%f, type=%d", progress, dataref->type);
                    success = false;
                }
            } else {
                // ... this shouldn't ever happen but we need to abort in that case
                RCLOG_WARN("[DRMU] unsupported method %d during computation", command->method);
                success = false;
            }
        }
        
        RCLOG_TRACE("[DRMU] writing results");
        if (success && !write_dataref(command, dataref, submit_values)) {
            RCLOG_WARN("[DRMU] failed to update %s (type %d, offset %d)", dataref->name, dataref->type, dataref->target_array_offset);
            success = false;
        }

        if (success && (command->monitor_mode == DRMU_MONITOR_GET)) {
            RCLOG_TRACE("[DRMU] updating monitor values");
            if (!read_dataref(dataref, dataref->buffer_values)) {
                RCLOG_WARN("[DRMU] failed to read monitor values for %s (type %d, offset %d)", dataref->name, dataref->type, dataref->target_array_offset);
                success = false;
            }
        }
        
        dataref = dataref->next;
        all_success &= success && monitor_copy_success;
        
        RCLOG_TRACE("[DRMU] dataref complete (all_success=%d, success=%d, monitor_copy_success=%d)", all_success, success, monitor_copy_success);
    }

    if (!all_success) {
        error_channel(command->session, command->channel_id, command->timestamp, "error during update");
        goto error;
    }

    command->update_base_values = false;

 exit:
    if (locked_proxy_registry) {
        RCLOG_TRACE("[DRMU] unlocking dataproxy registry");
        unlock_dataproxy_registry(command->session->server->config.dataproxy_registry);
    }

    return;
    
 error:
    command->failed = true;
    goto exit;
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

static void drmu_process_post(command_drmu_t *command) {
    if (!command->failed) {
        if (!command->initialized && !command->done) {
            return;
        }

        if (command->timestamp > 0) {
            submit_monitor_data(command, command->done ? DRMU_WILL_NOT_CONTINUE : DRMU_WILL_CONTINUE);
        }
    }
    
    if (command->failed) {
        // we probably have already sent an error message but should make sure that
        // we are really closing the channel as -ERR
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, NULL);
        command->done = true;
    }
    
    if (command->done) {
        drmu_terminate(command);
    }
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
            RCLOG_WARN("[DRMU] complete_without_schedule setting value failed (type %d, offset %d, error %d): %s", dataref->type, dataref->target_array_offset, err, dataref->name);
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
                    RCLOG_WARN("[DRMU] complete_without_schedule failed to copy target to monitor data");
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
                            RCLOG_WARN("[DRMU] complete_without_schedule failed to reduce monitor array size to %d (target length: %d)", num_copied, dataref->target_values->length);
                        }
                    }
                }
                
                if (err != ERROR_NONE) {
                    // still continue trying to set other datarefs; monitor will be skipped
                    success = false;
                    RCLOG_WARN("[DRMU] complete_without_schedule failed to get monitor data (type %d, offset %d, error %d): %s", dataref->type, dataref->target_array_offset, err, dataref->name);
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
        RCLOG_TRACE("%s is null", prefix);
        return;
    }

    RCLOG_TRACE("%s has length %d", prefix, arr->length);
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
            RCLOG_TRACE("%s encoding failed", prefix);
            return;
        }

        RCLOG_TRACE("%s: %s", prefix, s);
        free(s);
        return;
    }
    
    if (type != xplmType_Double) {
        RCLOG_TRACE("%s has unhandled type for debug output: %d", prefix, type);
        return;
    }

    for (int i=0; i<arr->length; i++) {
        RCLOG_TRACE("%s[%d] = %f", prefix, i, dynamic_array_get_item(xpdouble_t, arr, i));
    }
}
*/

static error_t drmu_create(void **command_ref, session_t *session, request_t *request, command_config_t *config) {
    error_t err = ERROR_NONE;
    error_t out_error = ERROR_NONE;
    
    int64_t now = millis_since_reference(session);
    if (now < 0) {
        return ERROR_UNSPECIFIC;
    }
    
    channel_id_t channel_id = request->channel_id;

    if (config->version != DRMU_COMMAND_VERSION) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unexpected command version");
        return ERROR_UNSPECIFIC;
    }

    if (!request_has_only_options(request, (char**)drmu_supported_options)) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unsupported options");
        return ERROR_UNSPECIFIC;
    }
    
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

    RCLOG_DEBUG("[DRMU] create: phase=%d, interval=%d (frames? %d), duration=%d (frames? %d), method=%d, monitor=%d", command->phase, command->interval, command->is_interval_frames, command->duration, command->is_duration_frames, command->method, command->monitor_mode); // DEBUG

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
        
        RCLOG_TRACE("[DRMU] create, dataref: name=\"%s\", type=%d, target_array_offset=%d", dataref->name, dataref->type, dataref->target_array_offset);

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

        //debug_dump("[DRMU] create, dataref target_values", dataref->type, dataref->target_values); // DEBUG

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
        } else if (command->method != DRMU_METHOD_IMMEDIATE) {
            // no base values provided but method requires them - fetch from dataref on initialization
            dataref->base_on_dataref = true;
            command->has_fetch_based_datarefs = true;
            
            dataref->base_values = create_dynamic_array(type_size, dataref->target_values->length);
            if (!dataref->base_values || !dynamic_array_set_length(dataref->base_values, dataref->target_values->length)) {
                error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "failed to allocate base values");
                goto error;
            }
        }

        //debug_dump("[DRMU] create, dataref base_values", dataref->type, dataref->base_values); // DEBUG

        dataref->proxy = find_registered_dataproxy(session->server->config.dataproxy_registry, dataref->name);
        if (dataref->proxy) {
            command->has_proxied_datarefs = true;
            
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

        RCLOG_TRACE("[DRMU] create, dataref proxy=%p", dataref->proxy);

        parameter = parameter->next;
        dataref_ref = &dataref->next;
    }

    *command_ref = command;

    bool needs_timing = (command->method != DRMU_METHOD_IMMEDIATE) || (command->interval != 0);
    bool needs_scheduling = needs_timing || !has_only_internal_datarefs;
    RCLOG_TRACE("[DRMU] create needs_timing=%d, needs_scheduling=%d", needs_timing, needs_scheduling);
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

static command_config_t* drmu_create_default_config() {
    return create_command_config(DRMU_COMMAND_VERSION);
}

static error_t drmu_merge_config(command_config_t **new_config, char **err_msg, command_config_t *previous_config, command_config_t *requested_changes) {
    if (requested_changes->version != DRMU_COMMAND_VERSION) {
        *err_msg = dynamic_sprintf("only supported version is %u, requested %u", DRMU_COMMAND_VERSION, requested_changes->version);
        return ERROR_UNSPECIFIC;
    }

    if (has_command_feature_flags(requested_changes)) {
        *err_msg = dynamic_sprintf("current command implementation does not support any feature flags");
        return ERROR_UNSPECIFIC;
    }

    return ERROR_NONE;
}

command_t command_drmu = {
    .name = "DRMU",
    .create = drmu_create,
    .terminate = drmu_terminate,
    .destroy = drmu_destroy,
    .create_default_config = drmu_create_default_config,
    .merge_config = drmu_merge_config,
};
