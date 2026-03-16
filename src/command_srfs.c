#include "command_srfs.h"

#include <limits.h>
#include <string.h>

#include "logger.h"
#include "session.h"
#include "utils.h"

#define SRFS_COMMAND_VERSION 1

static const char *srfs_supported_options[] = {
    NULL
};

static error_t srfs_destroy(void *command_ref) {
    RCLOG_WARN("[SRFS] unexpected call to destructor (command completes instantly and does not require destruction)");
    
    if (command_ref) {
        RCLOG_WARN("[SRFS] attempted destruction with unexpected command_ref %p, possible memory corruption", command_ref);
        return ERROR_UNSPECIFIC;
    }
    
    return ERROR_NONE;
}

static error_t srfs_terminate(void *command_ref) {
    RCLOG_WARN("[SRFS] unexpected request to terminate (command completes instantly and does not require termination/destruction)");
    
    if (command_ref) {
        RCLOG_WARN("[SRFS] attempted termination with unexpected command_ref %p, possible memory corruption", command_ref);
        return ERROR_UNSPECIFIC;
    }

    return ERROR_NONE;
}

static error_t srfs_create(void **command_ref, session_t *session, request_t *request, command_config_t *config) {
    error_t err = ERROR_NONE;
    error_t out_error = ERROR_NONE;
    command_config_t *requested_changes = NULL;
    command_config_t *merged_config = NULL;
    char *err_msg = NULL;

    channel_id_t channel_id = request->channel_id;
    
    if (config->version != SRFS_COMMAND_VERSION) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unexpected SRFS command version");
        return ERROR_UNSPECIFIC;
    }

    *command_ref = NULL;

    if (!request_has_only_options(request, (char**)srfs_supported_options)) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unsupported options");
        return ERROR_UNSPECIFIC;
    }
    
    command_parameter_t *parameter = request->parameters;
    if (!parameter || !parameter->parameter || strlen(parameter->parameter) == 0) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "missing parameter: expected command name as first parameter");
        return ERROR_UNSPECIFIC;
    }
    char *command_name = parameter->parameter;

    command_config_t *previous_config = get_command_configuration(session, command_name);
    if (!previous_config) {
        RCLOG_WARN("[SRFS] no previous configuration found for command \"%s\"", command_name);
        return ERROR_UNSPECIFIC;
    }

    RCLOG_DEBUG("[SRFS] parsing request for command \"%s\"", command_name);

    parameter = parameter->next;
    if (!parameter || !parameter->parameter || strlen(parameter->parameter) == 0) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "missing parameter: expected command version as second parameter");
        return ERROR_UNSPECIFIC;
    }
    RCLOG_DEBUG("[SRFS] parsing command version: \"%s\"", parameter->parameter);
    int requested_command_version = -1;
    if (!parse_int(&requested_command_version, parameter->parameter) || requested_command_version < 1) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "invalid command version");
        return ERROR_UNSPECIFIC;
    }
    RCLOG_DEBUG("[SRFS] parsed command version: %d", requested_command_version);

    requested_changes = create_command_config(requested_command_version);
    if (!requested_command_version) {
        RCLOG_WARN("[SRFS] failed to create command config");
        return ERROR_UNSPECIFIC;
    }

    parameter = parameter->next;
    if (!parameter || strlen(parameter->parameter) == 0) {
        RCLOG_INFO("[SRFS] requested reconfiguration for %s, version %d", command_name, requested_command_version);
    } else {
        if (parameter->next) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "too many parameters");
            goto error;
        }

        RCLOG_DEBUG("[SRFS] parsing feature flags: \"%s\"", parameter->parameter);

        size_t parameter_length = strlen(parameter->parameter);
        if (parameter_length > INT_MAX) {
            RCLOG_WARN("[SRFS] requested parameters exceed string address limit");
            goto error;
        }

        RCLOG_INFO("[SRFS] requested reconfiguration for %s, version %d: %s", command_name, requested_command_version, parameter->parameter);

        int start_offset = 0;
        while (start_offset < parameter_length) {
            int end_offset_excl = strpos(parameter->parameter, ",", start_offset);
            if (end_offset_excl < 0) {
                end_offset_excl = (int)parameter_length;
            }

            int selection_length = end_offset_excl - start_offset;

            // selection has to consist of one status flag (+ or -) and at least one character for the flag name
            if (selection_length < 2) {
                error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "invalid feature flag length");
                goto error;
            }

            feature_state_t state = COMMAND_FEATURE_STATE_NONE;
            char state_ch = parameter->parameter[start_offset];
            if (state_ch == '+') {
                state = COMMAND_FEATURE_STATE_ENABLE;
            } else if (state_ch == '-') {
                state = COMMAND_FEATURE_STATE_DISABLE;
            } else {
                error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "invalid or missing feature flag state");
                goto error;
            }

            // replace delimiter by null-termination to access as regular string
            parameter->parameter[end_offset_excl] = 0;

            char *flag_name = &parameter->parameter[start_offset+1];

            RCLOG_DEBUG("[SRFS] parsed feature flag: state=%c, flag_name=\"%s\"", state, flag_name);

            feature_state_t previous_state = COMMAND_FEATURE_STATE_NONE;
            err = set_command_feature_flag(&previous_state, requested_changes, flag_name, state);
            if (err != ERROR_NONE) {
                RCLOG_WARN("[SRFS] failed to set feature flag (%d)", err);
                goto error;
            } else if (previous_state != COMMAND_FEATURE_STATE_NONE) {
                error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "multiple changes requested for same feature flag");
                goto error;
            }

            start_offset = end_offset_excl + 1;
        }
    }

    err = merge_command_config(&merged_config, &err_msg, session->server->config.command_factory, command_name, previous_config, requested_changes);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[SRFS] failed to merge configuration changes for command %s, err=%d, err_msg: %s", command_name, err, err_msg);
        out_error = err;

        // try to include error message issued by command in response
        if (err_msg) {
            char *tmp = dynamic_sprintf("failed to reconfigure command: %s", err_msg);
            if (tmp) {
                error_channel(session, channel_id, CURRENT_TIME_REFERENCE, tmp);

                free(tmp);
                tmp = NULL;
            } else {
                // clear original message to fall back to generic handling below
                free(err_msg);
                err_msg = NULL;

                RCLOG_WARN("[SRFS] failed to format response error message");
            }
        }

        // respond with general error message if detail message was unavailable or could not be formatted
        if (!err_msg) {
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "failed to reconfigure command");
        }

        goto error;
    }

    if (!merged_config) {
        RCLOG_DEBUG("[SRFS] no changes to command configuration for %s", command_name);
    } else {
        err = set_command_configuration(session, command_name, merged_config);
        if (err != ERROR_NONE) {
            RCLOG_WARN("[SRFS] failed to reconfigure command %s (%d)", command_name, err);
            error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "failed to reconfigure command");
            goto error;
        }

        previous_config = NULL; // was freed by setter
        merged_config = NULL; // has been taken over by session, must not be freed
    }

    finish_channel(session, channel_id, CURRENT_TIME_REFERENCE, NULL);

    goto end;

error:
    RCLOG_INFO("[SRFS] request could not be processed");

    error_channel(session, channel_id, CURRENT_TIME_REFERENCE, NULL);

    if (out_error == ERROR_NONE) {
        out_error = ERROR_UNSPECIFIC;
    }

end:
    destroy_command_config(requested_changes);
    requested_changes = NULL;

    destroy_command_config(merged_config);
    merged_config = NULL;

    if (err_msg) {
        free(err_msg);
        err_msg = NULL;
    }

    return out_error;
}

static command_config_t* srfs_create_default_config() {
    return create_command_config(SRFS_COMMAND_VERSION);
}

static error_t srfs_merge_config(command_config_t **new_config, char **err_msg, command_config_t *previous_config, command_config_t *requested_changes) {
    if (requested_changes->version != SRFS_COMMAND_VERSION) {
        *err_msg = dynamic_sprintf("only supported version is %u, requested %u", SRFS_COMMAND_VERSION, requested_changes->version);
        return ERROR_UNSPECIFIC;
    }

    if (has_command_feature_flags(requested_changes)) {
        *err_msg = dynamic_sprintf("current command implementation does not support any feature flags");
        return ERROR_UNSPECIFIC;
    }

    return ERROR_NONE;
}

command_t command_srfs = {
    .name = "SRFS",
    .create = srfs_create,
    .terminate = srfs_terminate,
    .destroy = srfs_destroy,
    .create_default_config = srfs_create_default_config,
    .merge_config = srfs_merge_config,
};
