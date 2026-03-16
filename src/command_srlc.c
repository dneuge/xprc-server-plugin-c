#include "command_srlc.h"

#include <string.h>

#include "_buildinfo.h"
#include "logger.h"
#include "session.h"
#include "utils.h"

#define SRLC_COMMAND_VERSION 1
#define SRLC_COMMAND_NAME "SRLC"

static const char *srlc_supported_options[] = {
    NULL
};

static error_t srlc_destroy(void *command_ref) {
    RCLOG_WARN("[SRLC] unexpected call to destructor (command completes instantly and does not require destruction)");
    
    if (command_ref) {
        RCLOG_WARN("[SRLC] attempted destruction with unexpected command_ref %p, possible memory corruption", command_ref);
        return ERROR_UNSPECIFIC;
    }
    
    return ERROR_NONE;
}

static error_t srlc_terminate(void *command_ref) {
    RCLOG_WARN("[SRLC] unexpected request to terminate (command completes instantly and does not require termination/destruction)");
    
    if (command_ref) {
        RCLOG_WARN("[SRLC] attempted termination with unexpected command_ref %p, possible memory corruption", command_ref);
        return ERROR_UNSPECIFIC;
    }

    return ERROR_NONE;
}

static error_t send_command_configuration(session_t *session, channel_id_t channel_id, char *command_name, bool finish) {
    error_t out_error = ERROR_NONE;

    command_config_t *config = get_command_configuration(session, command_name);
    if (!config) {
        RCLOG_ERROR("[SRLC] command %s has no configuration (memory corruption or session not properly initialized)", command_name);
        return ERROR_UNSPECIFIC;
    }

    if (config->version < 1) {
        RCLOG_ERROR("[SRLC] invalid version %u found for command %s at %p (memory corruption?)", config->version, command_name, config);
        return ERROR_UNSPECIFIC;
    }

    char *msg = dynamic_sprintf("%s:%u", command_name, config->version);
    if (!msg) {
        return ERROR_MEMORY_ALLOCATION;
    }

    list_t *feature_flag_names = reference_feature_flag_names(config);
    if (!feature_flag_names) {
        RCLOG_WARN("[SRLC] failed to retrieve list of feature flag names for %s", command_name);
        goto error;
    }

    bool is_first = true;
    for (list_item_t *name_item = feature_flag_names->head; name_item; name_item = name_item->next) {
        char *name = name_item->value;
        if (!name) {
            RCLOG_ERROR("[SRLC] feature flag listed without name at %p (memory corruption?)", name_item);
            goto error;
        }

        char delimiter = ',';
        if (is_first) {
            delimiter = ':';
            is_first = false;
        }

        char state_encoded = 0;
        feature_state_t state = get_command_feature_state(config, name);
        if (state == COMMAND_FEATURE_STATE_ENABLED_DEFAULT) {
            state_encoded = '*';
        } else if (state == COMMAND_FEATURE_STATE_DISABLED_DEFAULT) {
            state_encoded = '?';
        } else if (state == COMMAND_FEATURE_STATE_ENABLED_CLIENT) {
            state_encoded = '+';
        } else if (state == COMMAND_FEATURE_STATE_DISABLED_CLIENT) {
            state_encoded = '-';
        } else if (state == COMMAND_FEATURE_STATE_UNAVAILABLE) {
            state_encoded = '/';
        } else if (state != COMMAND_FEATURE_STATE_ENABLED_ALWAYS) {
            RCLOG_ERROR("[SRLC] feature flag %s has unexpected/non-encodable state '%c'", name, state);
            goto error;
        }

        char *tmp = NULL;
        if (state_encoded) {
            tmp = dynamic_sprintf("%s%c%c%s", msg, delimiter, state_encoded, name);
        } else {
            tmp = dynamic_sprintf("%s%c%s", msg, delimiter, name);
        }
        if (!tmp) {
            RCLOG_WARN("[SRLC] failed to format feature flag");
            out_error = ERROR_MEMORY_ALLOCATION;
            goto error;
        }
        free(msg);
        msg = tmp;
        tmp = NULL;
    }

    if (finish) {
        out_error = finish_channel(session, channel_id, CURRENT_TIME_REFERENCE, msg);
    } else {
        out_error = continue_channel(session, channel_id, CURRENT_TIME_REFERENCE, msg);
    }
    if (out_error != ERROR_NONE) {
        RCLOG_WARN("[SRLC] failed to send message: %d", out_error);
        goto error;
    }

    goto end;

error:
    if (out_error == ERROR_NONE) {
        out_error = ERROR_UNSPECIFIC;
    }

end:
    if (feature_flag_names) {
        destroy_list(feature_flag_names, NULL); // names are shared references, must not be freed
        feature_flag_names = NULL;
    }

    if (msg) {
        free(msg);
        msg = NULL;
    }

    return out_error;
}

static error_t srlc_create(void **command_ref, session_t *session, request_t *request, command_config_t *config) {
    error_t err = ERROR_NONE;
    error_t out_error = ERROR_NONE;
    list_t *command_names = NULL;

    channel_id_t channel_id = request->channel_id;

    if (config->version != SRLC_COMMAND_VERSION) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unexpected command version");
        return ERROR_UNSPECIFIC;
    }

    *command_ref = NULL;

    if (!request_has_only_options(request, (char**)srlc_supported_options)) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unsupported options");
        return ERROR_UNSPECIFIC;
    }

    command_parameter_t *parameter = request->parameters;
    if (parameter) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unsupported parameters");
        return ERROR_UNSPECIFIC;
    }

    command_factory_t *command_factory = session->server->config.command_factory;
    if (!command_factory) {
        RCLOG_ERROR("[SRLC] command factory is unavailable (command invoked after server shutdown?)");
        goto error;
    }

    command_names = list_command_names(command_factory);
    if (!command_names) {
        RCLOG_ERROR("[SRLC] command factory at %p returned no list (memory corruption?)", command_factory);
        goto error;
    }

    // confirm channel without data on first message for better readability of raw transcripts
    confirm_channel(session, channel_id, CURRENT_TIME_REFERENCE, NULL);

    // own command must be described first (see specification)
    err = send_command_configuration(session, channel_id, SRLC_COMMAND_NAME, !command_names->head);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[SRLC] failed to send command configuration for %s, err=%d", SRLC_COMMAND_NAME, err);
        out_error = ERROR_INCOMPLETE;
        goto error;
    }

    for (list_item_t *name_item = command_names->head; name_item; name_item = name_item->next) {
        char *name = name_item->value;
        if (!name) {
            RCLOG_WARN("[SRLC] null command name returned by factory");
            continue;
        }

        // skip own command (already sent)
        if (!strcmp(SRLC_COMMAND_NAME, name)) {
            continue;
        }

        err = send_command_configuration(session, channel_id, name, !name_item->next);
        if (err != ERROR_NONE) {
            RCLOG_WARN("[SRLC] failed to send command configuration for %s, err=%d", name, err);
            out_error = ERROR_INCOMPLETE;
            goto error;
        }
    }

    goto end;

error:
    if (out_error == ERROR_NONE) {
        out_error = ERROR_UNSPECIFIC;
    }

    error_channel(session, channel_id, CURRENT_TIME_REFERENCE, NULL);

end:
    if (command_names) {
        destroy_list(command_names, free);
        command_names = NULL;
    }

    return out_error;
}

static command_config_t* srlc_create_default_config() {
    return create_command_config(SRLC_COMMAND_VERSION);
}

static error_t srlc_merge_config(command_config_t **new_config, char **err_msg, command_config_t *previous_config, command_config_t *requested_changes) {
    if (requested_changes->version != SRLC_COMMAND_VERSION) {
        *err_msg = dynamic_sprintf("only supported version is %u, requested %u", SRLC_COMMAND_VERSION, requested_changes->version);
        return ERROR_UNSPECIFIC;
    }

    if (has_command_feature_flags(requested_changes)) {
        *err_msg = dynamic_sprintf("current command implementation does not support any feature flags");
        return ERROR_UNSPECIFIC;
    }

    return ERROR_NONE;
}

command_t command_srlc = {
    .name = SRLC_COMMAND_NAME,
    .create = srlc_create,
    .terminate = srlc_terminate,
    .destroy = srlc_destroy,
    .create_default_config = srlc_create_default_config,
    .merge_config = srlc_merge_config,
};
