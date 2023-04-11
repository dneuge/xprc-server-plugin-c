#include "command_drci.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <XPLMDataAccess.h>
#include <XPLMPlugin.h>

#include "arrays.h"
#include "dataproxy.h"
#include "protocol.h"
#include "session.h"
#include "utils.h"
#include "xptypes.h"

#define DRCI_ECHO_NONE 0
#define DRCI_ECHO_OTHER 1
#define DRCI_ECHO_ALL 2
typedef uint8_t drci_echo_mode_t;

#define DRCI_INTCONV_NOT_SET 0
#define DRCI_INTCONV_ROUND 1
#define DRCI_INTCONV_FLOOR 2
#define DRCI_INTCONV_CEIL 3
typedef uint8_t drci_intconv_mode_t;

#define DRCI_REGISTRATION_PHASE TASK_SCHEDULE_BEFORE_FLIGHT_MODEL

#define PLUGIN_ID_DATAREFEDITOR "xplanesdk.examples.DataRefEditor"
#define DATAREFEDITOR_MSG_ADD_DATAREF 0x01000000

// TODO: range & range fit
// TODO: step & step fit
// TODO: mutex TERM + post-proc registration task

typedef struct {
    session_t *session;
    channel_id_t channel_id;

    mtx_t mutex;
    dataproxy_t *proxy;
    
    char *dataref_name;
    XPLMDataTypeID types;

    drci_echo_mode_t echo_mode;

    drci_intconv_mode_t intconv_mode;
    xpint_t value_int;
    xpfloat_t value_float;
    xpdouble_t value_double;
    int array_length;
    dynamic_array_t *values_int;
    dynamic_array_t *values_float;
    dynamic_array_t *blob;
    
    task_t *registration_task;
    bool registered;
    bool failed;
} command_drci_t;

static const dataproxy_operations_t drci_dataproxy_operations;

static error_t drci_destroy(void *command_ref) {
    printf("[XPRC] [DRCI] destroy\n"); // DEBUG
    
    if (!command_ref) {
        return ERROR_UNSPECIFIC;
    }
    
    command_drci_t *command = command_ref;

    if (command->dataref_name) {
        free(command->dataref_name);
        command->dataref_name = NULL;
    }

    if (command->values_int) {
        destroy_dynamic_array(command->values_int);
        command->values_int = NULL;
    }
    
    if (command->values_float) {
        destroy_dynamic_array(command->values_float);
        command->values_float = NULL;
    }
    
    if (command->blob) {
        destroy_dynamic_array(command->blob);
        command->blob = NULL;
    }
    
    printf("[XPRC] [DRCI] destroy: freeing command\n"); // DEBUG
    free(command);
    
    printf("[XPRC] [DRCI] destroy: done\n"); // DEBUG

    return ERROR_NONE;
}

static error_t drci_terminate(void *command_ref) {
    printf("[XPRC] [DRCI] terminate\n"); // DEBUG
    
    if (!command_ref) {
        return ERROR_UNSPECIFIC;
    }
    
    error_t err = ERROR_NONE;
    command_drci_t *command = command_ref;

    // channel may have been closed before (by error or finishing); ignore error
    finish_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, NULL);

    if (command->registration_task) {
        printf("[XPRC] [DRCI] terminate: have registration task, unscheduling\n"); // DEBUG
        task_schedule_t *task_schedule = command->session->server->config.task_schedule;
        
        err = lock_schedule(task_schedule);
        if (err == ERROR_NONE) {
            err = unschedule_task(task_schedule, command->registration_task, DRCI_REGISTRATION_PHASE);
            unlock_schedule(task_schedule);
        }
        
        if (err != ERROR_NONE) {
            printf("[XPRC] [DRCI] terminate failed to unschedule registration task: %d\n", err); // DEBUG
            return err;
        }
        
        printf("[XPRC] [DRCI] terminate: freeing registration task\n"); // DEBUG
        free(command->registration_task);
        command->registration_task = NULL;
    }

    if (command->proxy) {
        // proxy cannot be unregistered as termination is usually not called from XP context,
        // we need to rely on deferred deregistration by dropping the proxy instead
        err = drop_dataproxy(command->proxy);
        if (err != ERROR_NONE) {
            printf("[XPRC] [DRCI] failed to drop dataproxy: %d\n", err);
            return err;
        }
        
        command->proxy = NULL;
    }

    channel_id_t channel_id = command->channel_id;
    
    printf("[XPRC] [DRCI] terminate: poisoning channel ID\n"); // DEBUG
    command->channel_id = BAD_CHANNEL_ID;
    
    request_channel_destruction(command->session->channels, channel_id);
    
    return ERROR_NONE;
}

static void notify_datarefeditor(command_drci_t *command) {
    XPLMPluginID plugin_id = XPLMFindPluginBySignature(PLUGIN_ID_DATAREFEDITOR);
    if (plugin_id == XPLM_NO_PLUGIN_ID) {
        return;
    }

    XPLMSendMessageToPlugin(plugin_id, DATAREFEDITOR_MSG_ADD_DATAREF, command->dataref_name);
}

static void drci_process_flightloop(command_drci_t *command) {
    if (command->failed || command->registered) {
        return;
    }

    error_t err = register_dataproxy(command->proxy);
    if (err != ERROR_NONE) {
        printf("[XPRC] [DRCI] failed to register dataref for %s (error %d)\n", command->dataref_name, err);
        command->failed = true;
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "dataref could not be registered");
        return;
    }

    command->registered = true;

    err = confirm_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, NULL);
    if (err != ERROR_NONE) {
        printf("[XPRC] [DRCI] failed to send confirmation for %s (error %d)\n", command->dataref_name, err);
        command->failed = true;
    }

    notify_datarefeditor(command);
}

static char* encode_value(XPLMDataTypeID type, void *value_ref) {
    if (type == xplmType_IntArray || type == xplmType_FloatArray || type == xplmType_Data) {
        return xprc_encode_array(type, value_ref);
    } else {
        return xprc_encode_value(type, value_ref, (type == xplmType_Double) ? SIZE_XPLM_DOUBLE : SIZE_XPLM_INT_FLOAT);
    }
}

static void drci_process_post(command_drci_t *command) {
    if (command->failed) {
        drci_terminate(command);
        return;
    }
    
    if (!command->registered) {
        return;
    }
    
    task_schedule_t *task_schedule = command->session->server->config.task_schedule;
        
    error_t err = lock_schedule(task_schedule);
    if (err == ERROR_NONE) {
        err = unschedule_task(task_schedule, command->registration_task, DRCI_REGISTRATION_PHASE);
        unlock_schedule(task_schedule);
    }

    if (err == ERROR_NONE) {
        command->registration_task = NULL;
    } else {
        printf("[XPRC] [DRCI] failed to unschedule registration task for %s (error %d)\n", command->dataref_name, err);
        command->failed = true;
    }
}

static void drci_process(task_t *task, task_schedule_phase_t phase) {
    command_drci_t *command = task->reference;

    if (phase == DRCI_REGISTRATION_PHASE) {
        drci_process_flightloop(command);
    } else if (phase == TASK_SCHEDULE_POST_PROCESSING) {
        drci_process_post(command);
    }
}

static bool parse_permission(dataproxy_permission_t *permission, char *s) {
    if (!strcmp(s, "all")) {
        *permission = DATAPROXY_PERMISSION_ALL;
        return true;
    } else if (!strcmp(s, "xprc")) {
        *permission = DATAPROXY_PERMISSION_XPRC;
        return true;
    } else if (!strcmp(s, "session")) {
        *permission = DATAPROXY_PERMISSION_SESSION;
        return true;
    }

    return false;
}

static bool parse_echo_mode(drci_echo_mode_t *echo_mode, char *s) {
    if (!strcmp(s, "all")) {
        *echo_mode = DRCI_ECHO_ALL;
        return true;
    } else if (!strcmp(s, "other")) {
        *echo_mode = DRCI_ECHO_OTHER;
        return true;
    } else if (!strcmp(s, "none")) {
        *echo_mode = DRCI_ECHO_NONE;
        return true;
    }

    return false;
}

static bool parse_intconv_mode(drci_intconv_mode_t *intconv_mode, char *s) {
    if (!s) {
        *intconv_mode = DRCI_INTCONV_NOT_SET;
        return true;
    } else if (!strcmp(s, "round")) {
        *intconv_mode = DRCI_INTCONV_ROUND;
        return true;
    } else if (!strcmp(s, "floor")) {
        *intconv_mode = DRCI_INTCONV_FLOOR;
        return true;
    } else if (!strcmp(s, "ceil")) {
        *intconv_mode = DRCI_INTCONV_CEIL;
        return true;
    }

    return false;
}


const XPLMDataTypeID simple_types = xplmType_Int | xplmType_Float | xplmType_Double;
const XPLMDataTypeID array_types = xplmType_IntArray | xplmType_FloatArray | xplmType_Data;
const XPLMDataTypeID supported_types = simple_types | array_types;

static error_t drci_create(void **command_ref, session_t *session, request_t *request) {
    error_t err = ERROR_NONE;
    error_t out_error = ERROR_NONE;
    
    channel_id_t channel_id = request->channel_id;
    
    command_drci_t *command = zalloc(sizeof(command_drci_t));
    if (!command) {
        return ERROR_MEMORY_ALLOCATION;
    }

    if (mtx_init(&command->mutex, mtx_plain) != thrd_success) {
        free(command);
        return ERROR_UNSPECIFIC;
    }

    command->session = session;
    command->channel_id = channel_id;

    dataproxy_permission_t write_permission = DATAPROXY_PERMISSION_SESSION;
    if (!parse_permission(&write_permission, request_get_option(request, "writable", "all"))) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "invalid mode for writable");
        goto error;
    }

    if (!parse_echo_mode(&command->echo_mode, request_get_option(request, "echo", "other"))) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "invalid mode for echo");
        goto error;
    }

    if (!parse_intconv_mode(&command->intconv_mode, request_get_option(request, "intConv", NULL))) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "invalid mode for intConv");
        goto error;
    }

    if (request_get_option(request, "range", NULL)) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "range option is not supported yet");
        goto error;
    }
    
    if (request_get_option(request, "rangeFit", NULL)) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "rangeFit option is not supported yet");
        goto error;
    }

    if (request_get_option(request, "step", NULL)) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "step option is not supported yet");
        goto error;
    }
    
    if (request_get_option(request, "stepFit", NULL)) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "stepFit option is not supported yet");
        goto error;
    }
    
    // TODO: mutual exclusion of intConv and step options

    if (command->intconv_mode == DRCI_INTCONV_NOT_SET) {
        command->intconv_mode = DRCI_INTCONV_ROUND;
    }
    
    command_parameter_t *parameter = request->parameters;
    if (!parameter || parameter->next) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "exactly one dataref has to be defined");
        goto error;
    }

    // offset_type_separator separates types and name (mandatory)
    int offset_type_separator = strpos_unescaped(parameter->parameter, ":", 0);
    if (offset_type_separator < 1) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "dataref type must be specified");
        goto error;
    }

    // offset_name_separator separates name and array length (optional)
    int offset_name_separator = strpos_unescaped(parameter->parameter, ":", offset_type_separator + 1);
    int arrlen_length = (offset_name_separator >= 0)
        ? strlen(parameter->parameter) - offset_name_separator - 1 // array length goes to end of param
        : -1; // array length not found
    int escaped_name_length = (offset_name_separator < 0)
        ? strlen(parameter->parameter) - offset_type_separator - 1 // name goes to end of param
        : offset_name_separator - offset_type_separator - 1; // name goes only to length separator

    command->types = xprc_parse_types(parameter->parameter, offset_type_separator);
    printf("[XPRC] [DRCI] parsed types %d\n", command->types); // DEBUG

    bool has_simple_type = ((command->types & simple_types) != 0);
    bool has_array_type = ((command->types & array_types) != 0);
    if (has_simple_type && has_array_type) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "combination of simple and array types is not supported");
        goto error;
    } else if (command->types == xplmType_Unknown || (command->types & ~supported_types) != 0) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unknown or unsupported types");
        goto error;
    } else if (has_array_type && arrlen_length < 1) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "undefined array length");
        goto error;
    } else if ((command->types & xplmType_Data) != 0 && (command->types & ~xplmType_Data) != 0) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "blob cannot be combined with other types");
        goto error;
    }

    command->array_length = (arrlen_length > 0) ? atoi(&parameter->parameter[offset_name_separator+1]) : -1;
    if (has_array_type && command->array_length < 0) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "invalid array length");
        goto error;
    }

    printf("[XPRC] [DRCI] copy name\n"); // DEBUG
    command->dataref_name = copy_partial_unescaped_string(&parameter->parameter[offset_type_separator+1], escaped_name_length);
    if (!command->dataref_name) {
        printf("[XPRC] [DRCI] copy failed\n"); // DEBUG
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "dataref name could not be copied");
        out_error = ERROR_MEMORY_ALLOCATION;
        goto error;
    }
    printf("[XPRC] [DRCI] copy succeeded\n"); // DEBUG

    printf("[XPRC] [DRCI] array length: %d\n", command->array_length); // DEBUG
    
    if ((command->types & xplmType_IntArray) != 0) {
        command->values_int = create_dynamic_array(SIZE_XPLM_INT, command->array_length);
        if (!command->values_int || !dynamic_array_set_length(command->values_int, command->array_length)) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "failed to allocate int[]");
            out_error = ERROR_MEMORY_ALLOCATION;
            goto error;
        }
    }
    
    if ((command->types & xplmType_FloatArray) != 0) {
        command->values_float = create_dynamic_array(SIZE_XPLM_FLOAT, command->array_length);
        if (!command->values_float || !dynamic_array_set_length(command->values_float, command->array_length)) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "failed to allocate float[]");
            out_error = ERROR_MEMORY_ALLOCATION;
            goto error;
        }
    }
    
    if ((command->types & xplmType_Data) != 0) {
        command->blob = create_dynamic_array(1, command->array_length);
        if (!command->blob || !dynamic_array_set_length(command->blob, command->array_length)) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "failed to allocate blob array");
            out_error = ERROR_MEMORY_ALLOCATION;
            goto error;
        }
    }
    
    printf("[XPRC] [DRCI] reserving proxy\n"); // DEBUG
    command->proxy = reserve_dataproxy(session->server->config.dataproxy_registry, command->dataref_name, command->types, write_permission, command, session, drci_dataproxy_operations);
    if (!command->proxy) {
        printf("[XPRC] [DRCI] failed to reserve proxy\n"); // DEBUG
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "dataproxy could not be reserved (is the dataref already claimed?)");
        goto error;
    }
    printf("[XPRC] [DRCI] proxy reserved\n"); // DEBUG
    
    task_t *task = zalloc(sizeof(task_t));
    if (!task) {
        out_error = ERROR_MEMORY_ALLOCATION;
        goto error;
    }
    task->on_processing = drci_process;
    task->reference = command;

    printf("[XPRC] [DRCI] scheduling task\n"); // DEBUG

    err = lock_schedule(session->server->config.task_schedule);
    if (err == ERROR_NONE) {
        err = schedule_task(session->server->config.task_schedule, task, DRCI_REGISTRATION_PHASE);
        if (err == ERROR_NONE) {
            command->registration_task = task;
        }
        unlock_schedule(session->server->config.task_schedule);
    }
    
    if (err != ERROR_NONE) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "failed to schedule registration task");
        out_error = err;
        goto error;
    }

    *command_ref = command;
    
    printf("[XPRC] [DRCI] create done\n"); // DEBUG
    
    return ERROR_NONE;

 error:
    printf("[XPRC] [DRCI] create error handling\n"); // DEBUG
    if (command) {
        if (command->proxy) {
            err = release_dataproxy(command->proxy);
            if (err != ERROR_NONE) {
                printf("[XPRC] [DRCI] failed to release dataproxy on error handling during creation: %d\n", err);
            }
            command->proxy = NULL;
        }

        if (command->values_int) {
            destroy_dynamic_array(command->values_int);
            command->values_int = NULL;
        }

        if (command->values_float) {
            destroy_dynamic_array(command->values_float);
            command->values_float = NULL;
        }

        if (command->blob) {
            destroy_dynamic_array(command->blob);
            command->blob = NULL;
        }

        if (command->dataref_name) {
            free(command->dataref_name);
            command->dataref_name = NULL;
        }

        mtx_destroy(&command->mutex);

        free(command);
    }
    return (out_error != ERROR_NONE) ? out_error : ERROR_UNSPECIFIC;
}

static error_t drci_simple_get(void *ref, XPLMDataTypeID type, void *dest) {
    command_drci_t *command = ref;
    error_t out_err = ERROR_NONE;

    if (((command->types & type) == 0) || !dest) {
        return ERROR_UNSPECIFIC;
    }

    if (mtx_lock(&command->mutex) != thrd_success) {
        return ERROR_LOCK_FAILED;
    }
    
    if (type == xplmType_Int) {
        *((xpint_t*)dest) = command->value_int;
    } else if (type == xplmType_Float) {
        *((xpfloat_t*)dest) = command->value_float;
    } else if (type == xplmType_Double) {
        *((xpdouble_t*)dest) = command->value_double;
    } else {
        out_err = ERROR_UNSPECIFIC;
    }

    mtx_unlock(&command->mutex);
    
    return out_err;
}

static xpint_t float2int(float value, drci_intconv_mode_t mode) {
    if (mode == DRCI_INTCONV_FLOOR) {
        value = floorf(value);
    } else if (mode == DRCI_INTCONV_CEIL) {
        value = ceilf(value);
    }

    return (xpint_t) lroundf(value);
}

static xpint_t double2int(double value, drci_intconv_mode_t mode) {
    if (mode == DRCI_INTCONV_FLOOR) {
        value = floor(value);
    } else if (mode == DRCI_INTCONV_CEIL) {
        value = ceil(value);
    }

    return (xpint_t) lround(value);
}

static bool should_echo(command_drci_t *command, session_t *source_session) {
    if (command->echo_mode == DRCI_ECHO_OTHER) {
        return (source_session != command->session);
    }

    return (command->echo_mode == DRCI_ECHO_ALL);
}

static inline void dump_value(prealloc_list_t *list, command_drci_t *command, XPLMDataTypeID type, void *value_ref, int *total_length, bool *is_first, bool *should_abort) {
    if (*should_abort) {
        return;
    }

    if ((command->types & type) == 0) {
        return;
    }
    
    if (*is_first) {
        *is_first = false;
    } else {
        char *copy_separator = copy_string(";");
        if (!copy_separator) {
            goto error;
        }

        if (!prealloc_list_append(list, copy_separator)) {
            goto error;
        }
            
        *total_length = (*total_length) + 1;
    }
        
    char *encoded_value = encode_value(type, value_ref);
    if (!encoded_value) {
        goto error;
    }
        
    if (!prealloc_list_append(list, encoded_value)) {
        goto error;
    }
 
    *total_length = (*total_length) + strlen(encoded_value);

    return;

 error:
    *should_abort = true;
    return;
}

static char* concat(prealloc_list_t *list, int total_length) {
    if (total_length <= 0) {
        return NULL;
    }

    char *out = zalloc(total_length + 1);
    char *write = out;
    prealloc_list_item_t *item = list->first_in_use_item;
    while (item) {
        int len = strlen(item->value);
        memcpy(write, item->value, len);
        write += len;

        item = item->next_in_use;
    }
    
    return out;
}

static error_t dump_values(command_drci_t *command) {
    error_t err = ERROR_NONE;
    
    prealloc_list_t *list = create_preallocated_list();
    if (!list) {
        return ERROR_MEMORY_ALLOCATION;
    }

    bool is_first = true;
    bool should_abort = false;
    int total_length = 0;

    dump_value(list, command, xplmType_Int, &command->value_int, &total_length, &is_first, &should_abort);
    dump_value(list, command, xplmType_Float, &command->value_float, &total_length, &is_first, &should_abort);
    dump_value(list, command, xplmType_Double, &command->value_double, &total_length, &is_first, &should_abort);
    dump_value(list, command, xplmType_IntArray, command->values_int, &total_length, &is_first, &should_abort);
    dump_value(list, command, xplmType_FloatArray, command->values_float, &total_length, &is_first, &should_abort);
    dump_value(list, command, xplmType_Data, command->blob, &total_length, &is_first, &should_abort);

    if (should_abort) {
        err = ERROR_UNSPECIFIC;
    } else {
        char *s = concat(list, total_length);
        if (s) {
            err = continue_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, s);
        } else {
            err = ERROR_UNSPECIFIC;
        }
    }
    
    destroy_preallocated_list(list, free, PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS);

    return err;
}

static void apply_value(XPLMDataTypeID type, void *value, xpint_t *stored_int, xpfloat_t *stored_float, xpdouble_t *stored_double, drci_intconv_mode_t intconv_mode) {
    if (type == xplmType_Int) {
        xpint_t int_val = *((xpint_t*)value);
        if (stored_int) {
            *stored_int = int_val;
        }
        if (stored_float) {
            *stored_float = int_val;
        }
        if (stored_double) {
            *stored_double = int_val;
        }
    } else if (type == xplmType_Float) {
        xpfloat_t float_val = *((xpfloat_t*)value);
        if (stored_int) {
            *stored_int = float2int(float_val, intconv_mode);
        }
        if (stored_float) {
            *stored_float = float_val;
        }
        if (stored_double) {
            *stored_double = float_val;
        }
    } else if (type == xplmType_Double) {
        xpdouble_t double_val = *((xpdouble_t*)value);
        if (stored_int) {
            *stored_int = double2int(double_val, intconv_mode);
        }
        if (stored_float) {
            *stored_float = double_val;
        }
        if (stored_double) {
            *stored_double = double_val;
        }
    }
}

static error_t drci_simple_set(void *ref, XPLMDataTypeID type, void *value, session_t *source_session) {
    command_drci_t *command = ref;

    if (((command->types & type) == 0) || (type & ~simple_types) != 0 || !value) {
        return ERROR_UNSPECIFIC;
    }

    if (mtx_lock(&command->mutex) != thrd_success) {
        return ERROR_LOCK_FAILED;
    }

    apply_value(type, value, &command->value_int, &command->value_float, &command->value_double, command->intconv_mode);

    if (should_echo(command, source_session)) {
        dump_values(command);
    }

    mtx_unlock(&command->mutex);
    
    return ERROR_NONE;
}

static error_t get_actual_count(int *actual_count, int length, int offset, int count) {
    error_t out_err = ERROR_NONE;
    
    int available = length - offset;
    if (available < 0) {
        // out of bounds
        out_err = ERROR_UNSPECIFIC;
    }

    *actual_count = (available < count) ? available : count;
    if (*actual_count < 0) {
        // may not be out of bounds - excessive count is allowed
        *actual_count = 0;
    }

    return out_err;
}

static error_t drci_array_get(void *ref, XPLMDataTypeID type, void *dest, int *num_copied, int offset, int count) {
    command_drci_t *command = ref;
    error_t out_err = ERROR_NONE;

    if (((command->types & type) == 0) || !dest || !num_copied || offset < 0 || count < 0) {
        return ERROR_UNSPECIFIC;
    }

    if (count == 0) {
        *num_copied = 0;
        return ERROR_NONE;
    }

    if (mtx_lock(&command->mutex) != thrd_success) {
        return ERROR_LOCK_FAILED;
    }

    dynamic_array_t *arr = NULL;
    int type_size = SIZE_XPLM_INT_FLOAT;
    if (type == xplmType_IntArray) {
        arr = command->values_int;
    } else if (type == xplmType_FloatArray) {
        arr = command->values_float;
    } else if (type == xplmType_Data) {
        arr = command->blob;
        type_size = 1;
    }

    //printf("[XPRC] [DRCI] drci_array_get: arr=%p, type_size=%d\n", arr, type_size); // DEBUG

    if (!arr) {
        out_err = ERROR_UNSPECIFIC;
    } else {
        int actual_count = 0;
        out_err = get_actual_count(&actual_count, arr->length, offset, count);
        
        void *src = dynamic_array_get_pointer(arr, offset);
        //printf("[XPRC] [DRCI] drci_array_get: src=%p, actual_count=%d\n", src, actual_count); // DEBUG
        if ((out_err == ERROR_NONE) && src && (actual_count > 0)) {
            memcpy(dest, src, actual_count * type_size);
            *num_copied = actual_count;
        } else {
            *num_copied = 0;
        }
    }
    
    mtx_unlock(&command->mutex);
    
    //printf("[XPRC] [DRCI] drci_array_get: out_err=%d, num_copied=%d\n", out_err, *num_copied); // DEBUG
    
    return out_err;
}

static error_t drci_array_length(void *ref, XPLMDataTypeID type, int *length) {
    command_drci_t *command = ref;
    error_t out_err = ERROR_NONE;

    printf("[XPRC] [DRCI] drci_array_length\n"); // DEBUG
    
    if (((command->types & type) == 0) || !length) {
        return ERROR_UNSPECIFIC;
    }

    if (mtx_lock(&command->mutex) != thrd_success) {
        return ERROR_LOCK_FAILED;
    }

    dynamic_array_t *arr = NULL;
    if (type == xplmType_IntArray) {
        arr = command->values_int;
    } else if (type == xplmType_FloatArray) {
        arr = command->values_float;
    } else if (type == xplmType_Data) {
        arr = command->blob;
    }

    if (!arr) {
        printf("[XPRC] [DRCI] drci_array_length: no array\n"); // DEBUG
        out_err = ERROR_UNSPECIFIC;
    } else {
        printf("[XPRC] [DRCI] drci_array_length: %d\n", arr->length); // DEBUG
        *length = arr->length;
    }
    
    mtx_unlock(&command->mutex);
    
    return out_err;
}

static error_t drci_array_update(void *ref, XPLMDataTypeID type, void *values, int offset, int count, session_t *source_session) {
    command_drci_t *command = ref;
    error_t out_err = ERROR_NONE;

    //printf("[XPRC] [DRCI] drci_array_update: type=%d, values=%p, offset=%d, count=%d, source_session=%p\n", type, values, offset, count, source_session); // DEBUG
    
    if (((command->types & type) == 0) || (command->array_length < 0) || !values || offset < 0 || count < 0) {
        //printf("[XPRC] [DRCI] drci_array_update: bad precondition\n"); // DEBUG
        return ERROR_UNSPECIFIC;
    }

    if (count == 0) {
        //printf("[XPRC] [DRCI] drci_array_update: count 0\n"); // DEBUG
        return ERROR_NONE;
    }

    if (mtx_lock(&command->mutex) != thrd_success) {
        //printf("[XPRC] [DRCI] drci_array_update: lock failed\n"); // DEBUG
        return ERROR_LOCK_FAILED;
    }

    int actual_count = 0;
    out_err = get_actual_count(&actual_count, command->array_length, offset, count);

    //printf("[XPRC] [DRCI] drci_array_update: out_err=%d, actual_count=%d\n", out_err, actual_count); // DEBUG
    
    if ((out_err != ERROR_NONE) || (actual_count <= 0)) {
        //printf("[XPRC] [DRCI] drci_array_update: early quit\n"); // DEBUG
        mtx_unlock(&command->mutex);
        return out_err;
    }
    
    if (type == xplmType_Data) {
        // blobs do not support any type of value conversion and are mutually exclusive to other types
        // so the data can be copied directly
        void *dest = dynamic_array_get_pointer(command->blob, offset);
        //printf("[XPRC] [DRCI] drci_array_update: copy blob to %p\n", dest); // DEBUG
        memcpy(dest, values, actual_count);
    } else {
        int end = offset + actual_count;
        void *src = values;
        XPLMDataTypeID src_type = (type == xplmType_FloatArray) ? xplmType_Float : xplmType_Int;
        //printf("[XPRC] [DRCI] drci_array_update: copy values from src=%p, end=%d, src_type=%d\n", src, end, src_type); // DEBUG
        for (int i=offset; i<end; i++) {
            void *dest_int = command->values_int ? dynamic_array_get_pointer(command->values_int, i) : NULL;
            void *dest_float = command->values_float ? dynamic_array_get_pointer(command->values_float, i) : NULL;
            //printf("[XPRC] [DRCI] drci_array_update: copy value from src=%p (i=%d) to dest_int=%p, dest_float=%p\n", src, i, dest_int, dest_float); // DEBUG
            apply_value(src_type, src, dest_int, dest_float, NULL, command->intconv_mode);
            src += SIZE_XPLM_INT_FLOAT;
        }
    }
    
    //printf("[XPRC] [DRCI] drci_array_update: copy complete\n"); // DEBUG

    if (should_echo(command, source_session)) {
        //printf("[XPRC] [DRCI] drci_array_update: dumping\n"); // DEBUG
        dump_values(command);
    }
    
    mtx_unlock(&command->mutex);
    
    //printf("[XPRC] [DRCI] drci_array_update: done\n"); // DEBUG
    
    return out_err;
}

static const dataproxy_operations_t drci_dataproxy_operations = {
    .simple_get = drci_simple_get,
    .simple_set = drci_simple_set,
    .array_get = drci_array_get,
    .array_length = drci_array_length,
    .array_update = drci_array_update
};

command_t command_drci = {
    .name = "DRCI",
    .create = drci_create,
    .terminate = drci_terminate,
    .destroy = drci_destroy,
};
