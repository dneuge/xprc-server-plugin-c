#include "command.h"

#include "logger.h"
#include "utils.h"

bool is_command_feature_state_enabled(feature_state_t state) {
    return state == COMMAND_FEATURE_STATE_ENABLED_ALWAYS
        || state == COMMAND_FEATURE_STATE_ENABLED_CLIENT
        || state == COMMAND_FEATURE_STATE_ENABLED_DEFAULT;
}

bool is_command_feature_state_disabled(feature_state_t state) {
    return state == COMMAND_FEATURE_STATE_DISABLED_CLIENT
        || state == COMMAND_FEATURE_STATE_DISABLED_DEFAULT
        || state == COMMAND_FEATURE_STATE_UNAVAILABLE;
}

command_config_t* create_command_config(uint16_t version) {
    if (version < 1) {
        RCLOG_WARN("create_command_config called with invalid version %u", version);
        return NULL;
    }

    command_config_t *out = zmalloc(sizeof(command_config_t));
    if (!out) {
        return NULL;
    }

    out->version = version;

    out->_features = create_hashmap();
    if (!out->_features) {
        free(out);
        out = NULL;
    }

    return out;
}

void destroy_command_config(command_config_t *config) {
    if (!config) {
        return;
    }

    destroy_hashmap(config->_features, NULL); // using direct values (void* is not a pointer)
    free(config);
}

static bool is_valid_feature_state(feature_state_t state) {
    return state == COMMAND_FEATURE_STATE_ENABLE
        || state == COMMAND_FEATURE_STATE_DISABLE
        || state == COMMAND_FEATURE_STATE_ENABLED_DEFAULT
        || state == COMMAND_FEATURE_STATE_DISABLED_DEFAULT
        || state == COMMAND_FEATURE_STATE_ENABLED_CLIENT
        || state == COMMAND_FEATURE_STATE_DISABLED_CLIENT
        || state == COMMAND_FEATURE_STATE_UNAVAILABLE
        || state == COMMAND_FEATURE_STATE_ENABLED_ALWAYS;
}

error_t set_command_feature_flag(feature_state_t *previous_state, command_config_t *config, char *name, feature_state_t state) {
    if (!config) {
        RCLOG_WARN("set_command_feature_flag called without config");
        return ERROR_UNSPECIFIC;
    }

    if (!name) {
        RCLOG_WARN("set_command_feature_flag called without name");
        return ERROR_UNSPECIFIC;
    }

    if (!is_valid_feature_state(state)) {
        RCLOG_WARN("set_command_feature_flag: invalid state '%c'", state);
        return ERROR_UNSPECIFIC;
    }

    void *old_value = NULL; // must be void* for correct memory size written by hashmap_put (larger than feature_state_t)
    if (!hashmap_put(config->_features, name, (void*)(size_t)state, &old_value)) {
        RCLOG_WARN("set_command_feature_flag: failed to set \"%s\"", name);
        return ERROR_UNSPECIFIC;
    }

    if (previous_state) {
        *previous_state = (!old_value) ? COMMAND_FEATURE_STATE_NONE : (feature_state_t)(size_t)old_value;
    }

    return ERROR_NONE;
}

error_t add_command_feature_flag(command_config_t *config, char *name, feature_state_t state) {
    if (!config) {
        RCLOG_WARN("add_command_feature_flag called without config");
        return ERROR_UNSPECIFIC;
    }

    if (!name) {
        RCLOG_WARN("add_command_feature_flag called without name");
        return ERROR_UNSPECIFIC;
    }

    if (!is_valid_feature_state(state)) {
        RCLOG_WARN("add_command_feature_flag: invalid state '%c'", state);
        return ERROR_UNSPECIFIC;
    }

    // check before calling setter because trying to add an already existing flag should preserve state
    feature_state_t previous_state = get_command_feature_state(config, name);
    if (previous_state != COMMAND_FEATURE_STATE_NONE) {
        // already set
        return ERROR_UNSPECIFIC;
    }

    return set_command_feature_flag(NULL, config, name, state);
}

feature_state_t get_command_feature_state(command_config_t *config, char *name) {
    if (!config) {
        RCLOG_WARN("get_command_feature_state called without config");
        return COMMAND_FEATURE_STATE_NONE;
    }

    if (!name) {
        RCLOG_WARN("get_command_feature_state called without name");
        return COMMAND_FEATURE_STATE_NONE;
    }

    void *value = hashmap_get(config->_features, name);

    return (!value) ? COMMAND_FEATURE_STATE_NONE : (feature_state_t)(size_t)value;
}

bool has_command_feature_flags(command_config_t *config) {
    if (!config) {
        RCLOG_WARN("has_command_feature_flags called without config");
        return false;
    }

    return !is_hashmap_empty(config->_features);
}

list_t* reference_feature_flag_names(command_config_t *config) {
    if (!config) {
        RCLOG_WARN("reference_feature_flag_names called without config");
        return NULL;
    }

    return hashmap_reference_keys(config->_features);
}