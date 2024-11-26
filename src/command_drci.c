#include "command_drci.h"

#include <math.h>
#include <string.h>

#include "threads_compat.h"

#include <XPLMDataAccess.h>
#include <XPLMPlugin.h>

#include "arrays.h"
#include "dataproxy.h"
#include "logger.h"
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

#define DRCI_RANGEFIT_UNSUPPORTED 0
#define DRCI_RANGEFIT_LIMIT 1
#define DRCI_RANGEFIT_REJECT 2
#define DRCI_RANGEFIT_WRAP 3
typedef uint8_t drci_rangefit_mode_t;

#define DRCI_STEPFIT_UNSUPPORTED 0
#define DRCI_STEPFIT_CLOSEST 1
#define DRCI_STEPFIT_REJECT 2
#define DRCI_STEPFIT_LOWER 3
#define DRCI_STEPFIT_UPPER 4
typedef uint8_t drci_stepfit_mode_t;

#define DRCI_REGISTRATION_PHASE TASK_SCHEDULE_BEFORE_FLIGHT_MODEL

#define PLUGIN_ID_DATAREFEDITOR "xplanesdk.examples.DataRefEditor"
#define DATAREFEDITOR_MSG_ADD_DATAREF 0x01000000

#define DRCI_ITEM_SEPARATOR ","
#define DRCI_SUBITEM_SEPARATOR ":"

#define DRCI_STEP_DISABLE "*"

// TODO: mutex TERM + post-proc registration task

typedef struct {
    XPLMDataTypeID type;
    union {
        xpint_t int_value;
        xpfloat_t float_value;
        xpdouble_t double_value;
    };
} drci_vartype_t;

typedef struct {
    bool enabled;
    drci_stepfit_mode_t fit_mode;
    drci_vartype_t interval;
} drci_step_t;

typedef struct {
    drci_rangefit_mode_t fit_mode;
    bool minimum_bound;
    drci_vartype_t minimum;
    bool maximum_bound;
    drci_vartype_t maximum;
} drci_range_t;

typedef struct {
    session_t *session;
    channel_id_t channel_id;

    mtx_t mutex;
    dataproxy_t *proxy;
    
    char *dataref_name;
    XPLMDataTypeID types;

    drci_echo_mode_t echo_mode;
    dynamic_array_t *ranges;
    dynamic_array_t *steps;

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

static const char *drci_supported_options[] = {
    "echo",
    "intConv",
    "range",
    "rangeFit",
    "step",
    "stepFit",
    "writable",
    NULL
};

static error_t drci_destroy(void *command_ref) {
    RCLOG_TRACE("[DRCI] destroy");
    
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
    
    if (command->steps) {
        destroy_dynamic_array(command->steps);
        command->steps = NULL;
    }
    
    if (command->ranges) {
        destroy_dynamic_array(command->ranges);
        command->ranges = NULL;
    }
    
    RCLOG_TRACE("[DRCI] destroy: freeing command");
    free(command);
    
    RCLOG_TRACE("[DRCI] destroy: done");

    return ERROR_NONE;
}

static error_t drci_terminate(void *command_ref) {
    RCLOG_TRACE("[DRCI] terminate");
    
    if (!command_ref) {
        return ERROR_UNSPECIFIC;
    }
    
    error_t err = ERROR_NONE;
    command_drci_t *command = command_ref;

    // channel may have been closed before (by error or finishing); ignore error
    finish_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, NULL);

    if (command->registration_task) {
        RCLOG_TRACE("[DRCI] terminate: have registration task, unscheduling");
        task_schedule_t *task_schedule = command->session->server->config.task_schedule;
        
        err = lock_schedule(task_schedule);
        if (err == ERROR_NONE) {
            err = unschedule_task(task_schedule, command->registration_task, DRCI_REGISTRATION_PHASE);
            unlock_schedule(task_schedule);
        }
        
        if (err != ERROR_NONE) {
            RCLOG_WARN("[DRCI] terminate failed to unschedule registration task: %d", err);
            return err;
        }
        
        RCLOG_TRACE("[DRCI] terminate: freeing registration task");
        free(command->registration_task);
        command->registration_task = NULL;
    }

    if (command->proxy) {
        // proxy cannot be unregistered as termination is usually not called from XP context,
        // we need to rely on deferred deregistration by dropping the proxy instead
        err = drop_dataproxy(command->proxy);
        if (err != ERROR_NONE) {
            RCLOG_WARN("[DRCI] failed to drop dataproxy: %d", err);
            return err;
        }
        
        command->proxy = NULL;
    }

    channel_id_t channel_id = command->channel_id;
    
    RCLOG_TRACE("[DRCI] terminate: poisoning channel ID");
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
        RCLOG_WARN("[DRCI] failed to register dataref for %s (error %d)", command->dataref_name, err);
        command->failed = true;
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "dataref could not be registered");
        return;
    }

    command->registered = true;

    err = confirm_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, NULL);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[DRCI] failed to send confirmation for %s (error %d)", command->dataref_name, err);
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
        RCLOG_WARN("[DRCI] failed to unschedule registration task for %s (error %d)", command->dataref_name, err);
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

static drci_rangefit_mode_t parse_rangefit_mode(char *s, int count) {
    if (count == 5 && !strncmp(s, "limit", count)) {
        return DRCI_RANGEFIT_LIMIT;
    } else if (count == 6 && !strncmp(s, "reject", count)) {
        return DRCI_RANGEFIT_REJECT;
    } else if (count == 4 && !strncmp(s, "wrap", count)) {
        return DRCI_RANGEFIT_WRAP;
    } else {
        return DRCI_RANGEFIT_UNSUPPORTED;
    }
}

static error_t parse_variable_value(drci_vartype_t *var, XPLMDataTypeID type, char *s) {
    RCLOG_TRACE("[DRCI] parse_variable_value \"%s\"", s);
    
    var->type = type;
    
    if (type == xplmType_Int) {
        var->int_value = atoi(s);
    } else if (type == xplmType_Float) {
        var->float_value = atof(s);
    } else if (type == xplmType_Double) {
        var->double_value = atof(s); // atof actually returns a double
    } else {
        // invalidate
        var->type = xplmType_Unknown;
        return ERROR_UNSPECIFIC;
    }

    return ERROR_NONE;
}

static void vartype_to_xpdouble(drci_vartype_t *var, xpdouble_t *dest) {
    if (var->type == xplmType_Int) {
        *dest = var->int_value;
    } else if (var->type == xplmType_Float) {
        *dest = var->float_value;
    } else if (var->type == xplmType_Double) {
        *dest = var->double_value;
    }
}

static error_t parse_range(command_drci_t *command, drci_range_t *range, char *range_option, int count, XPLMDataTypeID *type_carry) {
    int num_separators = count_chars(range_option, DRCI_SUBITEM_SEPARATOR[0], count);
    bool has_previous_type = ((*type_carry & simple_types) != 0);
    bool omits_type = (num_separators == 1);
    RCLOG_TRACE("[DRCI] parse_range range=%p, count=%d, type_carry=%d, range_option: %s", range, count, *type_carry, range_option);
    if (omits_type) {
        if (!has_previous_type) {
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "range value type must be specified at least once");
            return ERROR_UNSPECIFIC;
        }
    } else if (num_separators != 2) {
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "invalid range syntax");
        return ERROR_UNSPECIFIC;
    }

    int min_offset = omits_type ? 0 : strpos(range_option, DRCI_SUBITEM_SEPARATOR, 0) + 1;
    RCLOG_TRACE("[DRCI] parse_range min_offset=%d @ %s", min_offset, &range_option[min_offset]);
    int max_offset = strpos(range_option, DRCI_SUBITEM_SEPARATOR, min_offset) + 1;
    RCLOG_TRACE("[DRCI] parse_range max_offset=%d @ %s", max_offset, &range_option[max_offset]);
    range->minimum_bound = ((max_offset - min_offset) > 1);
    range->maximum_bound = (max_offset < count);

    XPLMDataTypeID type = *type_carry;
    if (!omits_type) {
        int type_length = min_offset - 1;
        if (type_length < 1) {
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "range value type not specified");
            return ERROR_UNSPECIFIC;
        }

        type = xprc_parse_type(range_option, type_length);
        if ((type & simple_types) == 0) {
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "invalid range value type");
            return ERROR_UNSPECIFIC;
        }
        
        *type_carry = type;
    }

    error_t err = ERROR_NONE;
    
    if (range->minimum_bound) {
        // copy is needed for zero termination (otherwise number parsing could continue outside of current option)
        char *tmp = copy_partial_string(range_option + min_offset, max_offset - min_offset - 1);
        if (!tmp) {
            return ERROR_MEMORY_ALLOCATION;
        }
        
        err = parse_variable_value(&range->minimum, type, tmp);
        free(tmp);
        
        if (err != ERROR_NONE) {
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "failed to parse minimum range value");
            return err;
        }
    }
    
    if (range->maximum_bound) {
        // copy is needed for zero termination (otherwise number parsing could continue outside of current option)
        char *tmp = copy_partial_string(range_option + max_offset, count - max_offset);
        if (!tmp) {
            return ERROR_MEMORY_ALLOCATION;
        }
        
        err = parse_variable_value(&range->maximum, type, tmp);
        free(tmp);
        
        if (err != ERROR_NONE) {
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "failed to parse maximum range value");
            return err;
        }
    }

    if (range->minimum_bound && range->maximum_bound) {
        xpdouble_t minimum = 0.0;
        vartype_to_xpdouble(&range->minimum, &minimum);
        xpdouble_t maximum = 0.0;
        vartype_to_xpdouble(&range->maximum, &maximum);
        if (minimum > maximum) {
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "range maximum must not be larger than minimum");
            return ERROR_UNSPECIFIC;
        }
    }

    return ERROR_NONE;
}
     
static error_t parse_ranges(command_drci_t *command, char *range_option, char *rangefit_option) {
    if (!range_option) {
        return ERROR_NONE;
    }
    
    int num_ranges = count_chars(range_option, DRCI_ITEM_SEPARATOR[0], strlen(range_option)) + 1;
    int num_fitmodes = rangefit_option ? count_chars(rangefit_option, DRCI_ITEM_SEPARATOR[0], strlen(rangefit_option)) + 1 : 0;

    bool multiple_fitmodes = num_fitmodes > 1;

    if (multiple_fitmodes && (num_ranges != num_fitmodes)) {
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "number of range and rangeFit must match if multiple rangeFit options are specified");
        return ERROR_UNSPECIFIC;
    }

    command->ranges = create_dynamic_array(sizeof(drci_range_t), num_ranges); // will be freed by caller on error
    if (!command->ranges || !dynamic_array_set_length(command->ranges, num_ranges)) {
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "failed to allocate range definitions");
        return ERROR_MEMORY_ALLOCATION;
    }

    drci_rangefit_mode_t rangefit_mode = DRCI_RANGEFIT_LIMIT;
    int rangefit_separator = 0;
    XPLMDataTypeID range_value_type = xplmType_Unknown;
    for (int i=0; i<num_ranges; i++) {
        drci_range_t *range = dynamic_array_get_pointer(command->ranges, i);
        if (!range) {
            return ERROR_UNSPECIFIC;
        }
        
        if (i < num_fitmodes) { // runs once (single option) or for each (all set)
            rangefit_separator = strpos(rangefit_option, DRCI_ITEM_SEPARATOR, 0);
            if (rangefit_separator < 0) {
                // use full remaining string if separator was not found
                rangefit_separator = strlen(rangefit_option);
            }
            
            rangefit_mode = parse_rangefit_mode(rangefit_option, rangefit_separator);
            if (rangefit_mode == DRCI_RANGEFIT_UNSUPPORTED) {
                error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "unsupported rangeFit mode");
                return ERROR_UNSPECIFIC;
            }

            if (rangefit_separator > 0) {
                rangefit_option += rangefit_separator + 1;
            }
        }

        range->fit_mode = rangefit_mode;

        int range_separator = strpos(range_option, DRCI_ITEM_SEPARATOR, 0);
        if (range_separator < 0) {
            range_separator = strlen(range_option);
        }
        
        error_t err = parse_range(command, range, range_option, range_separator, &range_value_type);
        if (err != ERROR_NONE) {
            return err;
        }

        if ((range->fit_mode == DRCI_RANGEFIT_WRAP) && !(range->minimum_bound && range->maximum_bound)) {
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "rangeFit=wrap requires minimum and maximum value to be defined");
            return ERROR_UNSPECIFIC;
        }

        range_option += range_separator + 1;
    }

    if (RCLOG_IS_TRACE_ENABLED()) {
        RCLOG_TRACE("[DRCI] parsed %d ranges", num_ranges);
        for (int i=0; i<num_ranges; i++) {
            drci_range_t *range = dynamic_array_get_pointer(command->ranges, i);
            RCLOG_TRACE("[DRCI] range #%d: fit_mode=%d, min_bound=%d, minimum=[type=%d, int:%d, float:%f, double:%f], max_bound=%d, maximum=[type=%d, int:%d, float:%f, double:%f])", i, range->fit_mode, range->minimum_bound, range->minimum.type, range->minimum.int_value, range->minimum.float_value, range->minimum.double_value, range->maximum_bound, range->maximum.type, range->maximum.int_value, range->maximum.float_value, range->maximum.double_value);
        }
    }

    return ERROR_NONE;
}

static drci_stepfit_mode_t parse_stepfit_mode(char *s, int count) {
    if (count == 6 && !strncmp(s, "reject", count)) {
        return DRCI_STEPFIT_REJECT;
    } else if (count == 7 && !strncmp(s, "closest", count)) {
        return DRCI_STEPFIT_CLOSEST;
    } else if (count == 5 && !strncmp(s, "lower", count)) {
        return DRCI_STEPFIT_LOWER;
    } else if (count == 5 && !strncmp(s, "upper", count)) {
        return DRCI_STEPFIT_UPPER;
    } else {
        return DRCI_STEPFIT_UNSUPPORTED;
    }
}

static error_t parse_step(command_drci_t *command, drci_step_t *step, char *step_option, int count, XPLMDataTypeID *type_carry) {
    bool disable_step = (count == 1) && !strncmp(step_option, DRCI_STEP_DISABLE, count);
    if (disable_step) {
        RCLOG_TRACE("[DRCI] parse_step disabled, step_option: %s", step_option);
        return ERROR_NONE;
    }
    step->enabled = true;
    
    int num_separators = count_chars(step_option, DRCI_SUBITEM_SEPARATOR[0], count);
    bool has_previous_type = ((*type_carry & simple_types) != 0);
    bool omits_type = (num_separators == 0);
    RCLOG_TRACE("[DRCI] parse_step step=%p, count=%d, type_carry=%d, step_option: %s", step, count, *type_carry, step_option);
    
    if (omits_type) {
        if (!has_previous_type) {
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "step value type must be specified at least once");
            return ERROR_UNSPECIFIC;
        }
    } else if (num_separators != 1) {
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "invalid step syntax");
        return ERROR_UNSPECIFIC;
    }

    int step_offset = omits_type ? 0 : strpos(step_option, DRCI_SUBITEM_SEPARATOR, 0) + 1;

    XPLMDataTypeID type = *type_carry;
    if (!omits_type) {
        int type_length = step_offset - 1;
        if (type_length < 1) {
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "step value type not specified");
            return ERROR_UNSPECIFIC;
        }

        type = xprc_parse_type(step_option, type_length);
        if ((type & simple_types) == 0) {
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "invalid step value type");
            return ERROR_UNSPECIFIC;
        }
        
        *type_carry = type;
    }

    error_t err = ERROR_NONE;
    
    // copy is needed for zero termination (otherwise number parsing could continue outside of current option)
    char *tmp = copy_partial_string(step_option + step_offset, count - step_offset);
    if (!tmp) {
        return ERROR_MEMORY_ALLOCATION;
    }
        
    err = parse_variable_value(&step->interval, type, tmp);
    free(tmp);
        
    if (err != ERROR_NONE) {
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "failed to parse step value");
        return err;
    }

    return ERROR_NONE;
}

static error_t parse_steps(command_drci_t *command, char *step_option, char *stepfit_option) {
    if (!step_option) {
        return ERROR_NONE;
    }
    
    int num_steps = count_chars(step_option, DRCI_ITEM_SEPARATOR[0], strlen(step_option)) + 1;
    int num_fitmodes = stepfit_option ? count_chars(stepfit_option, DRCI_ITEM_SEPARATOR[0], strlen(stepfit_option)) + 1 : 0;

    bool multiple_fitmodes = num_fitmodes > 1;
    
    if (multiple_fitmodes && (num_steps != num_fitmodes)) {
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "number of step and stepFit must match if multiple stepFit options are specified");
        return ERROR_UNSPECIFIC;
    }

    command->steps = create_dynamic_array(sizeof(drci_step_t), num_steps); // will be freed by caller on error
    if (!command->steps || !dynamic_array_set_length(command->steps, num_steps)) {
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "failed to allocate step definitions");
        return ERROR_MEMORY_ALLOCATION;
    }

    drci_stepfit_mode_t stepfit_mode = DRCI_STEPFIT_CLOSEST;
    int stepfit_separator = 0;
    XPLMDataTypeID step_value_type = xplmType_Unknown;
    for (int i=0; i<num_steps; i++) {
        drci_step_t *step = dynamic_array_get_pointer(command->steps, i);
        if (!step) {
            return ERROR_UNSPECIFIC;
        }
        
        if (i < num_fitmodes) { // runs once (single option) or for each (all set)
            stepfit_separator = strpos(stepfit_option, DRCI_ITEM_SEPARATOR, 0);
            if (stepfit_separator < 0) {
                // use full remaining string if separator was not found
                stepfit_separator = strlen(stepfit_option);
            }
            
            stepfit_mode = parse_stepfit_mode(stepfit_option, stepfit_separator);
            if (stepfit_mode == DRCI_STEPFIT_UNSUPPORTED) {
                error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "unsupported stepFit mode");
                return ERROR_UNSPECIFIC;
            }

            if (stepfit_separator > 0) {
                stepfit_option += stepfit_separator + 1;
            }
        }

        step->fit_mode = stepfit_mode;

        int step_separator = strpos(step_option, DRCI_ITEM_SEPARATOR, 0);
        if (step_separator < 0) {
            step_separator = strlen(step_option);
        }
        
        error_t err = parse_step(command, step, step_option, step_separator, &step_value_type);
        if (err != ERROR_NONE) {
            return err;
        }

        step_option += step_separator + 1;
    }

    if (RCLOG_IS_TRACE_ENABLED()) {
        RCLOG_TRACE("[DRCI] parsed %d steps", num_steps);
        for (int i=0; i<num_steps; i++) {
            drci_step_t *step = dynamic_array_get_pointer(command->steps, i);
            RCLOG_TRACE("[DRCI] step #%d: enabled=%d, fit_mode=%d, step=[type=%d, int:%d, float:%f, double:%f]", i, step->enabled, step->fit_mode, step->interval.type, step->interval.int_value, step->interval.float_value, step->interval.double_value);
        }
    }

    return ERROR_NONE;
}

static error_t drci_create(void **command_ref, session_t *session, request_t *request) {
    error_t err = ERROR_NONE;
    error_t out_error = ERROR_NONE;
    
    channel_id_t channel_id = request->channel_id;

    if (!request_has_only_options(request, (char**)drci_supported_options)) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unsupported options");
        return ERROR_UNSPECIFIC;
    }
    
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

    #ifndef ENABLE_DRCI_RANGE
    if (request_get_option(request, "range", NULL)) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "range option is currently not supported");
        goto error;
    }
    
    if (request_get_option(request, "rangeFit", NULL)) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "rangeFit option is currently not supported");
        goto error;
    }
    #endif

    #ifndef ENABLE_DRCI_STEP
    if (request_get_option(request, "step", NULL)) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "step option is currently not supported");
        goto error;
    }
    
    if (request_get_option(request, "stepFit", NULL)) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "stepFit option is currently not supported");
        goto error;
    }
    #endif
    
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

    err = parse_ranges(command, request_get_option(request, "range", NULL), request_get_option(request, "rangeFit", NULL));
    if (err != ERROR_NONE) {
        goto error;
    }
    
    err = parse_steps(command, request_get_option(request, "step", NULL), request_get_option(request, "stepFit", NULL));
    if (err != ERROR_NONE) {
        goto error;
    }

    if (command->steps && (command->intconv_mode != DRCI_INTCONV_NOT_SET)) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "intConv and step are mutually exclusive");
        goto error;
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
    RCLOG_TRACE("[DRCI] parsed types %d", command->types);

    bool has_simple_type = ((command->types & simple_types) != 0);
    bool has_array_type = ((command->types & array_types) != 0);
    bool has_blob_type = ((command->types & xplmType_Data) != 0);
    if (has_simple_type && has_array_type) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "combination of simple and array types is not supported");
        goto error;
    } else if (command->types == xplmType_Unknown || (command->types & ~supported_types) != 0) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unknown or unsupported types");
        goto error;
    } else if (has_array_type && arrlen_length < 1) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "undefined array length");
        goto error;
    } else if (!has_array_type && arrlen_length > 0) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "length can only be specified for array types");
        goto error;
    } else if (has_blob_type && (command->types & ~xplmType_Data) != 0) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "blob cannot be combined with other types");
        goto error;
    }

    command->array_length = (arrlen_length > 0) ? atoi(&parameter->parameter[offset_name_separator+1]) : -1;
    if (has_array_type && command->array_length < 0) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "invalid array length");
        goto error;
    }

    bool valid_steps_length = !command->steps || (command->steps->length == 1) || (command->steps->length == command->array_length);
    if (!valid_steps_length) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "number of steps must be equal to array length, 1 or omitted");
        goto error;
    }
    
    bool valid_ranges_length = !command->ranges || (command->ranges->length == 1) || (command->ranges->length == command->array_length);
    if (!valid_ranges_length) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "number of ranges must be equal to array length, 1 or omitted");
        goto error;
    }

    if (has_blob_type) {
        if (command->intconv_mode != DRCI_INTCONV_NOT_SET) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "intConv option is not allowed on blobs");
            goto error;
        }
    
        if (command->ranges) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "range option is not allowed on blobs");
            goto error;
        }
    
        if (command->steps) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "step option is not allowed on blobs");
            goto error;
        }
    }
    
    if (command->intconv_mode == DRCI_INTCONV_NOT_SET) {
        command->intconv_mode = DRCI_INTCONV_ROUND;
    }
    
    // TODO: check if intConv with range leads to valid results

    RCLOG_TRACE("[DRCI] copy name");
    command->dataref_name = copy_partial_unescaped_string(&parameter->parameter[offset_type_separator+1], escaped_name_length);
    if (!command->dataref_name) {
        RCLOG_WARN("[DRCI] copy failed");
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "dataref name could not be copied");
        out_error = ERROR_MEMORY_ALLOCATION;
        goto error;
    }
    RCLOG_TRACE("[DRCI] copy succeeded");

    RCLOG_TRACE("[DRCI] array length: %d", command->array_length);
    
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
    
    RCLOG_TRACE("[DRCI] reserving proxy");
    command->proxy = reserve_dataproxy(session->server->config.dataproxy_registry, command->dataref_name, command->types, write_permission, command, session, drci_dataproxy_operations);
    if (!command->proxy) {
        RCLOG_TRACE("[DRCI] failed to reserve proxy");
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "dataproxy could not be reserved (is the dataref already claimed?)");
        goto error;
    }
    RCLOG_TRACE("[DRCI] proxy reserved");
    
    task_t *task = zalloc(sizeof(task_t));
    if (!task) {
        out_error = ERROR_MEMORY_ALLOCATION;
        goto error;
    }
    task->on_processing = drci_process;
    task->reference = command;

    RCLOG_TRACE("[DRCI] scheduling task");

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
    
    RCLOG_TRACE("[DRCI] create done");
    
    return ERROR_NONE;

 error:
    // FIXME: log specific warnings if something goes wrong
    RCLOG_TRACE("[DRCI] create error handling");
    if (command) {
        if (command->proxy) {
            err = release_dataproxy(command->proxy);
            if (err != ERROR_NONE) {
                RCLOG_WARN("[DRCI] failed to release dataproxy on error handling during creation: %d", err);
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

        if (command->steps) {
            destroy_dynamic_array(command->steps);
            command->steps = NULL;
        }
    
        if (command->ranges) {
            destroy_dynamic_array(command->ranges);
            command->ranges = NULL;
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
        return ERROR_MUTEX_FAILED;
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
    // TODO: support range
    // TODO: support step
    
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
        return ERROR_MUTEX_FAILED;
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
    if (available < 1) {
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
        return ERROR_MUTEX_FAILED;
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

    RCLOG_TRACE("[DRCI] drci_array_get: arr=%p, type_size=%d", arr, type_size);

    if (!arr) {
        out_err = ERROR_UNSPECIFIC;
    } else {
        int actual_count = 0;
        out_err = get_actual_count(&actual_count, arr->length, offset, count);
        
        void *src = dynamic_array_get_pointer(arr, offset);
        RCLOG_TRACE("[DRCI] drci_array_get: src=%p, actual_count=%d", src, actual_count);
        if ((out_err == ERROR_NONE) && src && (actual_count > 0)) {
            memcpy(dest, src, actual_count * type_size);
            *num_copied = actual_count;
        } else {
            *num_copied = 0;
        }
    }
    
    mtx_unlock(&command->mutex);
    
    RCLOG_TRACE("[DRCI] drci_array_get: out_err=%d, num_copied=%d", out_err, *num_copied);
    
    return out_err;
}

static error_t drci_array_length(void *ref, XPLMDataTypeID type, int *length) {
    command_drci_t *command = ref;
    error_t out_err = ERROR_NONE;

    RCLOG_TRACE("[DRCI] drci_array_length");
    
    if (((command->types & type) == 0) || !length) {
        return ERROR_UNSPECIFIC;
    }

    if (mtx_lock(&command->mutex) != thrd_success) {
        return ERROR_MUTEX_FAILED;
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
        RCLOG_WARN("[DRCI] drci_array_length: no array");
        out_err = ERROR_UNSPECIFIC;
    } else {
        RCLOG_TRACE("[DRCI] drci_array_length: %d", arr->length);
        *length = arr->length;
    }
    
    mtx_unlock(&command->mutex);
    
    return out_err;
}

static error_t drci_array_update(void *ref, XPLMDataTypeID type, void *values, int offset, int count, session_t *source_session) {
    command_drci_t *command = ref;
    error_t out_err = ERROR_NONE;

    RCLOG_TRACE("[DRCI] drci_array_update: type=%d, values=%p, offset=%d, count=%d, source_session=%p", type, values, offset, count, source_session);
    
    if (((command->types & type) == 0) || (command->array_length < 0) || !values || offset < 0 || count < 0) {
        RCLOG_WARN("[DRCI] drci_array_update: bad precondition");
        return ERROR_UNSPECIFIC;
    }

    if (count == 0) {
        RCLOG_TRACE("[DRCI] drci_array_update: count 0");
        return ERROR_NONE;
    }

    if (mtx_lock(&command->mutex) != thrd_success) {
        RCLOG_WARN("[DRCI] drci_array_update: lock failed");
        return ERROR_MUTEX_FAILED;
    }

    int actual_count = 0;
    out_err = get_actual_count(&actual_count, command->array_length, offset, count);

    RCLOG_TRACE("[DRCI] drci_array_update: out_err=%d, actual_count=%d", out_err, actual_count);
    
    if ((out_err != ERROR_NONE) || (actual_count <= 0)) {
        RCLOG_WARN("[DRCI] drci_array_update: early quit");
        mtx_unlock(&command->mutex);
        return out_err;
    }
    
    if (type == xplmType_Data) {
        // blobs do not support any type of value conversion and are mutually exclusive to other types
        // so the data can be copied directly
        void *dest = dynamic_array_get_pointer(command->blob, offset);
        RCLOG_TRACE("[DRCI] drci_array_update: copy blob to %p", dest);
        memcpy(dest, values, actual_count);
    } else {
        int end = offset + actual_count;
        void *src = values;
        XPLMDataTypeID src_type = (type == xplmType_FloatArray) ? xplmType_Float : xplmType_Int;
        RCLOG_TRACE("[DRCI] drci_array_update: copy values from src=%p, end=%d, src_type=%d", src, end, src_type);
        for (int i=offset; i<end; i++) {
            void *dest_int = command->values_int ? dynamic_array_get_pointer(command->values_int, i) : NULL;
            void *dest_float = command->values_float ? dynamic_array_get_pointer(command->values_float, i) : NULL;
            RCLOG_TRACE("[DRCI] drci_array_update: copy value from src=%p (i=%d) to dest_int=%p, dest_float=%p", src, i, dest_int, dest_float);
            apply_value(src_type, src, dest_int, dest_float, NULL, command->intconv_mode);
            src += SIZE_XPLM_INT_FLOAT;
        }
    }
    
    RCLOG_TRACE("[DRCI] drci_array_update: copy complete");

    if (should_echo(command, source_session)) {
        RCLOG_TRACE("[DRCI] drci_array_update: dumping");
        dump_values(command);
    }
    
    mtx_unlock(&command->mutex);
    
    RCLOG_TRACE("[DRCI] drci_array_update: done");
    
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
