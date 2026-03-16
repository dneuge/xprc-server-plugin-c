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

typedef struct _command_config_t command_config_t;
typedef struct _command_t command_t;

#include "errors.h"
#include "hashmap.h"
#include "requests.h"
#include "session.h"

/// see COMMAND_FEATURE_STATE_ macros for possible states
typedef char feature_state_t;

// NOTE: feature state macros mostly "coincide" (mainly for debugging) with protocol spec but
//       should not be relied upon for parsing or encoding

/// feature is currently enabled by default and can be disabled through SRFS
#define COMMAND_FEATURE_STATE_ENABLED_DEFAULT '*'
/// feature is currently disabled by default and can be enabled through SRFS
#define COMMAND_FEATURE_STATE_DISABLED_DEFAULT '?'
/// feature has been enabled by client and can be disabled again
#define COMMAND_FEATURE_STATE_ENABLED_CLIENT '+'
/// feature has been disabled by client and can be enabled again
#define COMMAND_FEATURE_STATE_DISABLED_CLIENT '-'
/// feature is unavailable (cannot be enabled)
#define COMMAND_FEATURE_STATE_UNAVAILABLE '/'
/// feature is always enabled (cannot be disabled)
#define COMMAND_FEATURE_STATE_ENABLED_ALWAYS ' '
/// placeholder returned by helper functions, not to be used on setter calls
#define COMMAND_FEATURE_STATE_NONE 0

/// feature has been requested to be enabled through SRFS
#define COMMAND_FEATURE_STATE_ENABLE COMMAND_FEATURE_STATE_ENABLED_CLIENT
/// feature has been requested to be disabled through SRFS
#define COMMAND_FEATURE_STATE_DISABLE COMMAND_FEATURE_STATE_DISABLED_CLIENT

/**
 * Describes a command feature flag.
 *
 * Can be used both to list current state and to request changes.
 */
typedef struct {
    feature_state_t state;
    char *name;
} command_feature_t;

/**
 * Checks if the given state indicates that a feature should be enabled.
 * @param state feature state to check
 * @return true if enabled, false if disabled or invalid/unknown
 */
bool is_command_feature_state_enabled(feature_state_t state);
/**
 * Checks if the given state indicates that a feature should be disabled.
 * @param state feature state to check
 * @return true if disabled, false if enabled or invalid/unknown
 */
bool is_command_feature_state_disabled(feature_state_t state);

/**
 * Describes a command configuration.
 *
 * Can be used both to list currently active configuration and to request changes.
 *
 * Feature flags should be accessed through helper functions #set_command_feature_flag
 * and #get_command_feature_flag.
 */
typedef struct _command_config_t {
    uint16_t version;
    hashmap_t *_features;
} command_config_t;

/**
 * Creates a new command configuration.
 * @param version command version to indicate (must be strictly positive)
 * @return new instance, NULL on error
 */
command_config_t* create_command_config(uint16_t version);

/**
 * Destroys the given command configuration incl. all feature states.
 * @param config command configuration to be destroyed; may be NULL
 */
void destroy_command_config(command_config_t *config);

/**
 * Sets the feature flag to the requested state.
 *
 * If the flag was already present, it will be indicated through previous_state,
 * otherwise #COMMAND_FEATURE_STATE_NONE will be applied. previous_state may also
 * be provided as NULL if the previous state should not be provided back.
 *
 * @param previous_state will be set to the previous state if replaced or #COMMAND_FEATURE_STATE_NONE if newly added; may be NULL
 * @param config command configuration to modify
 * @param name name of feature flag (will be copied; case-sensitive)
 * @param state new feature flag state
 * @return error code; #ERROR_NONE on success
 * @see #add_command_feature_flag
 */
error_t set_command_feature_flag(feature_state_t *previous_state, command_config_t *config, char *name, feature_state_t state);
/**
 * Adds the given feature flag in requested state if it does not already exist.
 *
 * This function fails if the feature flag is already present. In that case,
 * the original state will be maintained.
 *
 * @param config command configuration to modify
 * @param name name of feature flag (will be copied; case-sensitive)
 * @param state feature flag state
 * @return error code; #ERROR_NONE on success
 * @see #set_command_feature_flag
 */
error_t add_command_feature_flag(command_config_t *config, char *name, feature_state_t state);
/**
 * Returns the configured feature flag state.
 *
 * @param config command configuration to read from
 * @param name name of feature flag (case-sensitive)
 * @return configured state; #COMMAND_FEATURE_STATE_NONE if not set
 */
feature_state_t get_command_feature_state(command_config_t *config, char *name);
/**
 * Checks whether the given command configuration holds any feature flags.
 * @param config command configuration to check
 * @return true if at least one feature flag has been configured, false if none are set
 */
bool has_command_feature_flags(command_config_t *config);
/**
 * Returns references to all names currently present on given configuration.
 *
 * Memory management:
 * - list structure is to be managed by caller (destroy when no longer needed)
 * - list values are shared with configuration (must not be freed or altered)
 *
 * @param config command configuration to return feature flag names for
 * @return list of shared references to feature flag names (must not be freed); NULL on error
 */
list_t* reference_feature_flag_names(command_config_t *config);

/**
 * Allocates and initializes a new instance of the command.
 *
 * A pointer to the created command instance's data structure must be set on command_ref as it will be used on
 * subsequent calls regarding this command instance. Commands that can complete instantly may keep the command_ref
 * pointing to NULL if there actually is no instance and the command has already fully completed (either successfully
 * or failed).
 *
 * Commands are always instantiated through a request handled by a session. The #request_t provides access to command
 * options and parameters as requested by the client. The required information must be extracted/copied as the
 * #request_t is only valid for the duration of this call.
 *
 * The #session_t should be saved for communication and control. It is only provided once but will remain available
 * until the command has been destroyed.
 *
 * Commands may support versioning and feature flags, both optionally being configurable per session. If applicable,
 * the command must act according to the given #command_config_t effective for this invocation. If necessary, relevant
 * parts must be copied/persisted from the configuration as the given structure is managed by the caller and
 * must not be referenced after this call completes.
 *
 * #ERROR_NONE must be returned to indicate successful command creation (or instant completion).
 * If the command could not be created, for example due to invalid options/parameters requested by the client,
 * an appropriate error code must be used (#ERROR_NONE must not be used if failed).
 *
 * Clean up must be performed before returning in case command creation fails. In particular, all newly allocated memory
 * must be (eventually) freed again.
 *
 * @param command_ref must be set to the command instance when successful; will be provided from other functions
 * @param session the session that created this command; should be stored within the command instance, guaranteed to exist until command destruction
 * @param request the request that created this command, contains command configuration; must not be stored, will become unavailable at end of request
 * @param config the command configuration to use for this instance; must not be stored, may become unavailable at end of request
 * @return error code describing what prevented command creation; #ERROR_NONE on success
 */
typedef error_t (*command_create_f) (void **command_ref, session_t *session, request_t *request, command_config_t *config);
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
/**
 * Returns a new instance of the command's initial default configuration to be applied to new sessions.
 *
 * If a command is to be reconfigured later, this is the configuration that will be provided as previous_config on
 * first reconfiguration. The default configuration should never change but a new copy must be returned on each
 * invocation of this function.
 *
 * @return new instance describing the command's default configuration (memory management will be taken over by caller); NULL on error
 */
typedef command_config_t* (*command_create_default_config_f)();
/**
 * Merges the requested changes, if applicable, providing the merged result as new_config.
 *
 * new_config will be provided on command creation where effective. Commands must not start applying
 * any changes directly.
 *
 * err_msg can be set to an optional error message to be submitted to requesting clients. Must be kept as or set to NULL
 * if success (#ERROR_NONE) is being indicated.
 *
 * previous_config is guaranteed to have come from get_default_config or to have been written by a previous call to
 * reconfigure, no additional manipulation is being performed, thus contents can be trusted. If the new configuration
 * remains unchanged (same as previous_config), new_config may be left as-is (pointing to NULL).
 *
 * Memory is managed by caller, both for input and output parameters.
 *
 * @param new_config to be set to the updated configuration taking effect from next command instantiation (memory management will be taken over by caller; can be left NULL if no change is to be applied)
 * @param err_msg pointer (never NULL) for an optional error message to submit to requesting client if failed; point to NULL on success (memory management will be taken over by caller)
 * @param previous_config previous configuration to change from (memory managed by caller; must not be changed)
 * @param requested_changes requested configuration changes (memory managed by caller; must not be changed)
 * @return error code describing what prevented reconfiguration; #ERROR_NONE on success (also keep err_msg set to NULL)
 */
typedef error_t (*command_merge_config_f)(command_config_t **new_config, char **err_msg, command_config_t *previous_config, command_config_t *requested_changes);

/// command interface descriptor
typedef struct _command_t {
    /// the 4-letter command name as specified in XPRC protocol, all upper-case
    char *name;
    command_create_f create;
    command_terminate_f terminate;
    command_destroy_f destroy;
    command_create_default_config_f create_default_config;
    command_merge_config_f merge_config;
} command_t;

#endif
