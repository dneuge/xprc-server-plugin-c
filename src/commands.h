#ifndef COMMANDS_H
#define COMMANDS_H

/**
 * @file commands.h XPRC command factory
 *
 * The command factory keeps track of all available XPRC command implementations and provides a uniform way to create
 * new command instances.
 */

typedef struct _command_factory_t command_factory_t;

#include "command.h"
#include "channels.h"
#include "hashmap.h"

/// XPRC command factory instance
typedef struct _command_factory_t {
    /// all command descriptors indexed by their name
    hashmap_t *commands_by_name;
} command_factory_t;

/**
 * Creates a new command factory instance.
 * @return command factory instance; NULL on error
 */
command_factory_t* create_command_factory();
/**
 * Destroys the given command factory instance.
 * @param factory command factory to destroy
 */
void destroy_command_factory(command_factory_t *factory);

/**
 * Creates a new command instance and registers it to the given channel.
 * @param factory factory to look up command descriptors from
 * @param channel the channel to register the command instance to
 * @param session the session where the request originated from
 * @param request the request for command creation
 * @return error code; #ERROR_NONE if successful
 */
error_t create_command(command_factory_t *factory, channel_t *channel, session_t *session, request_t *request);

/**
 * Merges the requested changes based on the previous configuration, providing the result as new_config.
 * new_config may be kept set to NULL if the request did not lead to any changes (equal to previous_config).
 *
 * If a reference is provided for err_msg (not NULL) and the changes are being declined, err_msg may be set to a
 * specific message that can be logged or returned to clients. A failing change (as indicated by return value) may
 * not always provide an err_msg, instead (check error_t for success/failure indication instead of the err_msg pointer).
 *
 * previous_config must have come from get_default_config or a previous call to this function. If the new configuration
 * remains unchanged (same as previous_config), new_config may be left as-is (pointing to NULL).
 *
 * Memory is managed by caller, both for input and output parameters.
 *
 * @param new_config will be set to the updated configuration (may be NULL if no change has been applied)
 * @param err_msg optional error message to submit to requesting client if failed; points to NULL on success (must be freed by caller if set)
 * @param factory command factory
 * @param command_name name of command to update config for
 * @param previous_config previous configuration to change from
 * @param requested_changes requested configuration changes
 * @return error code describing what prevented reconfiguration; #ERROR_NONE on success
 */
error_t merge_command_config(command_config_t **new_config, char **err_msg, command_factory_t *factory, char *command_name, command_config_t *previous_config, command_config_t *requested_changes);

/**
 * Returns a new instance of the specified command's initial default configuration as to be applied to new sessions.
 *
 * Memory is managed by caller.
 *
 * @return new instance describing the command's default configuration; NULL on error
 */
command_config_t* create_default_command_config(command_factory_t *factory, char *command_name);

/**
 * Returns a list holding copies of the names of all commands registered to the factory.
 *
 * Memory is managed by caller; list and values must be freed when no longer needed.
 *
 * @param factory command factory
 * @return list of registered command names in no particular order; NULL on error
 */
list_t* list_command_names(command_factory_t *factory);

#endif
