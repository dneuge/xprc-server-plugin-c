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

#endif
