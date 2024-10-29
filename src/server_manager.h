#ifndef SERVER_MANAGER_H
#define SERVER_MANAGER_H

#include "server.h"
#include "settings_manager.h"

typedef enum {
    /// placeholder for when the state is not known/has not been set yet
    UNKNOWN = 0,
    /// the server should be or has been stopped (completed)
    STOPPED,
    /// the server should be or has been started (completed)
    STARTED,
    /// the server should be/is currently being restarted (ongoing)
    RESTARTING,
    /// the server should be or has been shut down (terminal operation to prepare destruction)
    SHUTDOWN
} managed_server_state_t;

/**
 * Checks if the given state indicates that the server is or should be running.
 * @return true if the given state indicates a running server (incl. pending requests to be started), false if not
 */
bool is_running_server_state(managed_server_state_t state);

/**
 * Called when the managed server state changes.
 * @param context same context reference as passed in during listener registration
 * @param new_state state the server manager has changed to
 */
typedef void (*server_state_listener_f)(void *context, managed_server_state_t new_state);

typedef struct {
    settings_manager_t *settings_manager;
    /// locally created and memory-managed copy of the provided base configuration
    server_config_t *server_config;

    server_t *server;
    /// set true if the requested state cannot be reached without manual user action; server manager only transitions
    /// states while set false
    bool user_intervention_needed;
    /// state changes are only processed while set to true
    bool change_state;
    /// indicates the requested state to reach first (true = server started, false = server stopped)
    bool wanted_state_first;
    /// indicates the next state to reach (true = server started, false = server stopped); stable if equal to first
    /// state
    bool wanted_state_next;
    /// used to shut down the manager instance to prepare destruction; blocks all but shutdown-related operations when
    /// set to true
    bool shutdown;
    /// used to indicate that all pending actions should be aborted ASAP as the manager instance is about to be
    /// destroyed
    bool destruction_pending;

    /// listener to notify about state changes
    server_state_listener_f state_listener;
    /// listener-provided reference, will be used on call to state_listener to provide context information
    void *state_listener_context;

    mtx_t mutex;
} server_manager_t;

/**
 * Creates a new server manager instance.
 * @param settings_manager used to retrieve user-specific configuration details from
 * @return server manager instance; NULL on error
 */
server_manager_t* create_server_manager(settings_manager_t *settings_manager);
/**
 * Destroys the given server manager instance.
 * @param server_manager instance to destroy
 * @return error code; #ERROR_NONE on success
 */
error_t destroy_server_manager(server_manager_t *server_manager);

/**
 * Provides the given server configuration to the server manager instance.
 * The original instance is being copied and needs to be memory-managed by the caller.
 * Any dependencies contained within the server config must not be destroyed before
 * the server manager has been successfully destroyed.
 * @param server_manager instance to configure
 * @param server_config base configuration to apply; will be copied
 * @return error code; #ERROR_NONE on success
 */
error_t provide_managed_server_base_config(server_manager_t *server_manager, server_config_t *server_config);
/**
 * Performs maintenance on the server manager. Must be called periodically to process events.
 * @param server_manager instance to maintain
 * @return error code; #ERROR_NONE on success
 */
error_t maintain_server_manager(server_manager_t *server_manager);

/**
 * Requests the server managed by given instance to be started.
 * @param server_manager instance to start server of
 * @return error code; #ERROR_NONE on success
 */
error_t start_managed_server(server_manager_t *server_manager);
/**
 * Requests the server managed by given instance to be stopped.
 * @param server_manager instance to stop server of
 * @return error code; #ERROR_NONE on success
 */
error_t stop_managed_server(server_manager_t *server_manager);
/**
 * Requests the server managed by given instance to be restarted.
 * @param server_manager instance to restart server of
 * @return error code; #ERROR_NONE on success
 */
error_t restart_managed_server(server_manager_t *server_manager);
/**
 * Requests the server managed by given instance to be shut down.
 * Successfully shutting down the server is required before destroying the manager instance.
 * @param server_manager instance to shut down server of
 * @return error code; #ERROR_NONE on success
 */
error_t shutdown_managed_server(server_manager_t *server_manager);

/**
 * Retrieves the current server state.
 * @param server_manager instance to query state of
 * @return current server state; #UNKNOWN on error
 */
managed_server_state_t get_managed_server_state(server_manager_t *server_manager);

/**
 * Registers the given listener to be notified about state changes.
 * The listener will not be called upon registration; if an initial state is required, it has to be queried separately.
 * @param server_manager instance to register listener to
 * @param listener listener to be notified
 * @param context reference to provide back to listener when notifying
 * @return error code; #ERROR_NONE on success
 */
error_t register_server_state_listener(server_manager_t *server_manager, server_state_listener_f listener, void *context);
/**
 * Removes the given listener. Listener and context reference must match exactly.
 * @param server_manager instance to remove listener from
 * @param listener listener to remove
 * @param context context reference of listener; must match exactly
 */
error_t unregister_server_state_listener(server_manager_t *server_manager, server_state_listener_f listener, void *context);

#endif
