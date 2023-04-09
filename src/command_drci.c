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

const XPLMDataTypeID supported_types = xplmType_Int | xplmType_Float | xplmType_Double | xplmType_IntArray | xplmType_FloatArray | xplmType_Data;

static bool is_supported_type_combination(XPLMDataTypeID types) {
    if (types == xplmType_Unknown) {
        return false;
    }

    return (types & ~supported_types) == 0;
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
    
    int offset_type_separator = strpos(parameter->parameter, ":", 0);
    if (offset_type_separator < 1) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "dataref type must be specified");
        goto error;
    }

    int name_length = strlen(parameter->parameter) - offset_type_separator - 1;
    if (name_length < 1) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "dataref name is missing");
        goto error;
    }

    command->types = xprc_parse_types(parameter->parameter, offset_type_separator);
    printf("[XPRC] [DRCI] parsed types %d\n", command->types); // DEBUG
    if (!is_supported_type_combination(command->types)) {
        printf("[XPRC] [DRCI] unsupported types\n"); // DEBUG
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unknown or unsupported types");
        goto error;
    }

    printf("[XPRC] [DRCI] copy name\n"); // DEBUG
    command->dataref_name = copy_string(&parameter->parameter[offset_type_separator+1]);
    if (!command->dataref_name) {
        printf("[XPRC] [DRCI] copy failed\n"); // DEBUG
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "dataref name could not be copied");
        out_error = ERROR_MEMORY_ALLOCATION;
        goto error;
    }
    printf("[XPRC] [DRCI] copy succeeded\n"); // DEBUG

    // TODO: allocate buffers
    
    /*
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
    */

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

        // TODO: free buffers

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

static error_t drci_simple_set(void *ref, XPLMDataTypeID type, void *value, session_t *source_session) {
    command_drci_t *command = ref;
    error_t out_err = ERROR_NONE;

    if (((command->types & type) == 0) || !value) {
        return ERROR_UNSPECIFIC;
    }

    if (mtx_lock(&command->mutex) != thrd_success) {
        return ERROR_LOCK_FAILED;
    }
    
    if (type == xplmType_Int) {
        command->value_int = *((xpint_t*)value);
        command->value_float = command->value_int;
        command->value_double = command->value_double;
    } else if (type == xplmType_Float) {
        command->value_float = *((xpfloat_t*)value);
        command->value_double = command->value_float;
        command->value_int = float2int(command->value_float, command->intconv_mode);
    } else if (type == xplmType_Double) {
        command->value_double = *((xpdouble_t*)value);
        command->value_float = command->value_double;
        command->value_int = double2int(command->value_double, command->intconv_mode);
    } else {
        out_err = ERROR_UNSPECIFIC;
    }

    if (out_err == ERROR_NONE && should_echo(command, source_session)) {
        dump_values(command);
    }
    
    mtx_unlock(&command->mutex);
    
    return out_err;
}

static error_t drci_array_get(void *ref, XPLMDataTypeID type, void *dest, int *num_copied, int offset, int count) {
    command_drci_t *command = ref;
    printf("[XPRC] [DRCI] array_get %d for %s\n", type, command->dataref_name); // DEBUG
    return ERROR_UNSPECIFIC;
}

static error_t drci_array_length(void *ref, XPLMDataTypeID type, int *length) {
    command_drci_t *command = ref;
    printf("[XPRC] [DRCI] array_length %d for %s\n", type, command->dataref_name); // DEBUG
    return ERROR_UNSPECIFIC;
}

static error_t drci_array_update(void *ref, XPLMDataTypeID type, void *values, int offset, int count, session_t *source_session) {
    command_drci_t *command = ref;
    printf("[XPRC] [DRCI] array_update %d for %s\n", type, command->dataref_name); // DEBUG
    return ERROR_UNSPECIFIC;
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
