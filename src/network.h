#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>
#include <stdint.h>

#include "errors.h"

/**
 * @file network.h
 * Defines a generic framework for line-based (telnet-style) TCP network servers.
 */

/// minimum server port number that can be bound to without elevated permissions
// TODO: may be platform-specific - no conclusive information found about what Windows does; testing needed when implemented
#define NETWORK_MINIMUM_PORT 1024
/// maximum server port number
// TODO: exclude typical ephemeral ports? (platform-specific)
#define NETWORK_MAXIMUM_PORT 65535

/// indicates that the server socket could not be created
#define NETWORK_ERROR_NO_SOCKET     (NETWORK_ERROR_BASE + 0)
/// indicates that the requested server address could not be bound to
#define NETWORK_ERROR_BIND_FAILED   (NETWORK_ERROR_BASE + 1)
/// indicates that listening on the server socket failed
#define NETWORK_ERROR_LISTEN_FAILED (NETWORK_ERROR_BASE + 2)
/// indicates that the server listening address is invalid (not found/understood by operating system)
#define NETWORK_ERROR_BAD_ADDRESS   (NETWORK_ERROR_BASE + 3)
/// indicates that the server failed to spawn or join one of its threads; should be treated as a fatal error requiring
/// application restart for cleanup
#define NETWORK_ERROR_THREAD_FAILED (NETWORK_ERROR_BASE + 4)
/// indicates that the server is being shutdown, no new actions can be performed
#define NETWORK_ERROR_SHUTDOWN      (NETWORK_ERROR_BASE + 5)
/// indicates that the action cannot be performed due to insufficient resources
#define NETWORK_ERROR_CAPACITY      (NETWORK_ERROR_BASE + 6)

/// usable as length for #send_to_network() to indicate that the entire string (until NULL termination) should be sent
#define NETWORK_SEND_COMPLETE_STRING -34768

// structures defined by implementation
/// a network server; exact structure is to be defined by OS-specific network implementation
typedef struct _network_server_t network_server_t;
/// a network connection; exact structure is to be defined by OS-specific network implementation
typedef struct _network_connection_t network_connection_t;

/**
 * Called when a new connection has been initiated.
 *
 * Network can already be sent to but lines can only be received after this call succeeds.
 * handler_reference must be set to something that can be used to identify the connection;
 * the connection reference should be saved.
 *
 * Constructor_reference is provided as configured in network_handler_t.
 *
 * @param connection the current network server connection; reference should be saved
 * @param handler_reference must be set to something that can be used to identify the connection, will be provided to other callbacks
 * @param constructor_reference reference as configured in network_handler_t
 * @return error code; #ERROR_NONE on success
 */
typedef error_t (*new_connection_f)(network_connection_t *connection, void **handler_reference, void *constructor_reference);

/**
 * Called for each line received from the network.
 *
 * @param handler_reference the reference as set during the new_connection_f callback when the connection was initiated
 * @param line received line; must only be read as indicated by length parameter, not terminated; must not be manipulated/freed
 * @param length number of characters that belong to the line; line end is omitted (not present on string either)
 */
typedef void (*on_line_received_f)(void *handler_reference, char *line, int length);

/**
 * Called via #close_network_connection() when network connection is closing.
 *
 * Connection may already have been partially closed.
 * This is the last call using handler_reference from network side.
 *
 * @param handler_reference the reference as set during the new_connection_f callback when the connection was initiated
 */
typedef void (*on_connection_closing_f)(void *handler_reference);

/**
 * Callback configuration for network event handling.
 */
typedef struct {
    /// called when a new client connects
    new_connection_f new_connection;
    /// will be provided on every call to #new_connection
    void *new_connection_constructor_reference;
    /// called when a new line has been received
    on_line_received_f on_line_received;
    /// called when a connection is being closed
    on_connection_closing_f on_connection_closing;
} network_handler_t;

/// interface name to only listen to connections from the same machine
#define INTERFACE_LOCAL "localhost"

/**
 * Holds the requested configuration for a new server instance.
 */
typedef struct {
    /// allows to listen on IPv6 if true, restricts to IPv4 if false
    bool enable_ipv6;
    /// listening interface address as null-terminated string; when set to NULL the server will listen on all interfaces
    char *interface;
    /// TCP port number to listen on
    uint16_t port;
} network_server_config_t;

/**
 * Creates and starts a new network server.
 *
 * @param server will be set to reference to server instance; must not be NULL
 * @param config reference to configuration, must not be NULL; only accessed momentarily (can be modified after this function exits)
 * @param handler handler callback configuration
 * @return error code; #ERROR_NONE on success
 */
error_t create_network_server(network_server_t **server, network_server_config_t *config, network_handler_t handler);

/**
 * Attempts to shut the network server down and free the instance.
 *
 * Although success/failure is indicated, shutdown must not be retried when failure has been indicated. This indication
 * should rather be used to determine that a fatal error has occurred within the application.
 *
 * @param server server instance to shutdown and destroy; must not be NULL
 * @return true if successful; false if stuck meaning memory is still assigned and threads may still be running
 */
bool destroy_network_server(network_server_t *server);

/**
 * Sends the given content over a connection.
 *
 * @param connection connection to send the content to; must not be NULL
 * @param content pointer to the content to be sent; must not be NULL
 * @param length length of the content to be sent; #NETWORK_SEND_COMPLETE_STRING will send until first null-termination
 * @return error code; #ERROR_NONE on success
 */
error_t send_to_network(network_connection_t *connection, char *content, int length);

/**
 * Terminates the specified connection on next occasion (delayed).
 *
 * @param connection connection to terminate; must not be NULL
 */
void close_network_connection(network_connection_t *connection);

#endif
