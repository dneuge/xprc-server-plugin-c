#ifndef COMMAND_H
#define COMMAND_H

/**
 * @file command.h interface descriptor for XPRC commands
 *
 * Command descriptors are used to register a command for generalized lookup and invocation. In order to be made
 * available within the plugin, each command has to implement and expose the functions and identify with a command name
 * by defining an according #command_t constant. That constant is then registered by and made available through the
 * factory defined by commands.h.
 *
 * Commands are instantiated from requests through #command_create_f after registering the channel on the session.
 * When successful, the command descriptor and command reference (which should generally be set to the command instance
 * data structure) is stored on the channel.
 *
 * Termination of channels and their associated command (#command_terminate_f) goes hand in hand: Requesting command or
 * channel termination is essentially the same on protocol level, neither can be used without the other. As commands
 * decide to terminate themselves they will have to #request_channel_destruction() to align their own destruction with
 * the channel, see channels.h.
 *
 * When a channel is being destroyed, #command_destroy_f will be invoked to also destroy the command instance. Commands
 * must not destroy instances on their own without that invocation, except for clean up during failed instance creation.
 */

typedef struct _command_t command_t;

#include "errors.h"
#include "requests.h"
#include "session.h"

/**
 * Allocates and initializes a new instance of the command.
 *
 * A pointer to the created command instance's data structure must be set on command_ref as it will be used on
 * subsequent calls regarding this command instance.
 *
 * Commands are always instantiated through a request handled by a session. The #request_t provides access to command
 * options and parameters as requested by the client. The required information must be extracted/copied as the
 * #request_t is only valid for the duration of this call.
 *
 * The #session_t should be saved for communication and control. It is only provided once but will remain available
 * until the command has been destroyed.
 *
 * #ERROR_NONE must be returned to indicate successful command creation. If the command could not be created, for
 * example due to invalid options/parameters requested by the client, an appropriate error code must be used
 * (#ERROR_NONE must not be used if failed).
 *
 * Clean up must be performed before returning in case command creation fails. In particular, all newly allocated memory
 * must be (eventually) freed again.
 *
 * @param command_ref must be set to the command instance when successful; will be provided from other functions
 * @param session the session that created this command; should be stored within the command instance, guaranteed to exist until command destruction
 * @param request the request that created this command, contains command configuration; must not be stored, will become unavailable at end of request
 * @return error code describing what prevented command creation; #ERROR_NONE on success
 */
typedef error_t (*command_create_f) (void **command_ref, session_t *session, request_t *request);
/**
 * Terminates the command meaning it stops/cancels all actions and dequeues from scheduling.
 *
 * During termination the command prepares for destruction but must still keep the command instance available.
 * Instead of destroying itself, #request_channel_destruction() should be used to align destruction with the channel,
 * see channels.h.
 *
 * Command termination should generally not fail. In case it really cannot be terminated, the whole session will be
 * shut down in an attempt to reduce side-effects. If the error is unrecoverable, a fatal error should be raised and
 * logged.
 *
 * @param command_ref command instance as set by #command_create_f
 * @return error code describing what prevented command termination; #ERROR_NONE on success
 */
typedef error_t (*command_terminate_f) (void *command_ref);
/**
 * Destroys the command instance, freeing all allocated resources.
 *
 * Destruction should generally not fail. In case it actually does, at least the session will be terminated as a whole.
 * If the error is unrecoverable, a fatal error should be raised and logged.
 *
 * @param command_ref command instance as set by #command_create_f
 * @return error code describing what prevented command destruction; #ERROR_NONE on success
 */
typedef error_t (*command_destroy_f) (void *command_ref);

/// command interface descriptor
typedef struct _command_t {
    /// the 4-letter command name as specified in XPRC protocol, all upper-case
    char *name;
    command_create_f create;
    command_terminate_f terminate;
    command_destroy_f destroy;
} command_t;

#endif
