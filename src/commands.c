#include <stdlib.h>
#include <string.h>

#include "commands.h"
#include "logger.h"

#include "command_cmhd.h"
#include "command_cmrg.h"
#include "command_cmtr.h"
#include "command_drci.h"
#include "command_drls.h"
#include "command_drqv.h"
#include "command_drmu.h"
#include "command_srfs.h"
#include "command_srid.h"
#include "command_srlc.h"

static bool register_command(command_factory_t *factory, command_t *command) {
    void *_old_value = NULL;
    return hashmap_put(factory->commands_by_name, command->name, command, &_old_value);
}

command_factory_t* create_command_factory() {
    command_factory_t *factory = malloc(sizeof(command_factory_t));
    if (!factory) {
        return NULL;
    }

    memset(factory, 0, sizeof(command_factory_t));

    factory->commands_by_name = create_hashmap();
    if (!factory->commands_by_name) {
        free(factory);
        return NULL;
    }

    bool success = true;
    success &= register_command(factory, &command_cmhd);
    success &= register_command(factory, &command_cmrg);
    success &= register_command(factory, &command_cmtr);
    success &= register_command(factory, &command_drci);
    success &= register_command(factory, &command_drls);
    success &= register_command(factory, &command_drqv);
    success &= register_command(factory, &command_drmu);
    success &= register_command(factory, &command_srfs);
    success &= register_command(factory, &command_srid);
    success &= register_command(factory, &command_srlc);

    if (!success) {
        destroy_command_factory(factory);
        return NULL;
    }

    return factory;
}

void destroy_command_factory(command_factory_t *factory) {
    destroy_hashmap(factory->commands_by_name, NULL);
    free(factory);
}

error_t create_command(command_factory_t *factory, channel_t *channel, session_t *session, request_t *request) {
    command_t *command = hashmap_get(factory->commands_by_name, request->command_name);
    if (!command) {
        return ERROR_UNSPECIFIC;
    }

    channel->command = command;

    command_config_t *config = get_command_configuration(session, request->command_name);
    if (!config) {
        RCLOG_ERROR("Command has no configuration: %s", request->command_name);
        return ERROR_UNSPECIFIC;
    }
    
    return command->create(&channel->command_ref, session, request, config);
}

error_t merge_command_config(command_config_t **new_config, char **err_msg, command_factory_t *factory, char *command_name, command_config_t *previous_config, command_config_t *requested_changes) {
    if (!new_config) {
        RCLOG_ERROR("merge_command_config called without new_config");
        return ERROR_UNSPECIFIC;
    }

    if (*new_config) {
        RCLOG_ERROR("merge_command_config called with new_config - refusing to run as it would likely introduce a memory leak (caller would loose track of instance: %p)", *new_config);
        return ERROR_UNSPECIFIC;
    }

    if (err_msg && *err_msg) {
        RCLOG_ERROR("merge_command_config called with err_msg - refusing to run as it would likely introduce a memory leak (caller would loose track of instance: %p)", *err_msg);
        return ERROR_UNSPECIFIC;
    }

    if (!factory) {
        RCLOG_ERROR("merge_command_config called without factory");
        return ERROR_UNSPECIFIC;
    }

    if (!command_name) {
        RCLOG_ERROR("merge_command_config called without command_name");
        return ERROR_UNSPECIFIC;
    }

    if (!previous_config) {
        RCLOG_ERROR("merge_command_config called without previous_config");
        return ERROR_UNSPECIFIC;
    }

    if (!requested_changes) {
        RCLOG_ERROR("merge_command_config called without requested_changes");
        return ERROR_UNSPECIFIC;
    }

    command_t *command = hashmap_get(factory->commands_by_name, command_name);
    if (!command) {
        RCLOG_WARN("merge_command_config: unknown command \"%s\"", command_name);
        return ERROR_UNSPECIFIC;
    }

    *new_config = NULL;
    char *merge_err_msg = NULL; // required when calling command implementation; must not forward err_msg
    error_t err = command->merge_config(new_config, &merge_err_msg, previous_config, requested_changes);

    if (err == ERROR_NONE && merge_err_msg) {
        RCLOG_WARN("command %s returned an error message but no error code; overriding error result to \"unspecific\"", command_name);
        err = ERROR_UNSPECIFIC;
    }

    if (err != ERROR_NONE && *new_config) {
        RCLOG_WARN("command %s indicates an error but returned a new configuration; invalidating result and error code (was %d)", command_name, err);
        err = ERROR_UNSPECIFIC;

        destroy_command_config(*new_config);
        *new_config = NULL;
    }

    // forward error message (may be NULL) if wanted by caller, otherwise free if the command actually provided a message
    if (err_msg) {
        *err_msg = merge_err_msg;
    } else if (merge_err_msg) {
        free(merge_err_msg);
        merge_err_msg = NULL;
    }

    return err;
}

command_config_t* create_default_command_config(command_factory_t *factory, char *command_name) {
    if (!factory) {
        RCLOG_ERROR("create_default_command_config called without factory");
        return NULL;
    }

    if (!command_name) {
        RCLOG_ERROR("create_default_command_config called without command_name");
        return NULL;
    }

    command_t *command = hashmap_get(factory->commands_by_name, command_name);
    if (!command) {
        RCLOG_WARN("create_default_command_config: unknown command \"%s\"", command_name);
        return NULL;
    }

    return command->create_default_config();
}

list_t* list_command_names(command_factory_t *factory) {
    if (!factory) {
        RCLOG_ERROR("list_command_names called without factory");
        return NULL;
    }

    return hashmap_copy_keys(factory->commands_by_name);
}
