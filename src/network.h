#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>
#include <stdint.h>

#include "errors.h"

#define NETWORK_ERROR_NO_SOCKET     (NETWORK_ERROR_BASE + 0)
#define NETWORK_ERROR_BIND_FAILED   (NETWORK_ERROR_BASE + 1)
#define NETWORK_ERROR_LISTEN_FAILED (NETWORK_ERROR_BASE + 2)
#define NETWORK_ERROR_BAD_ADDRESS   (NETWORK_ERROR_BASE + 3)
#define NETWORK_ERROR_THREAD_FAILED (NETWORK_ERROR_BASE + 4)
#define NETWORK_ERROR_SHUTDOWN      (NETWORK_ERROR_BASE + 5)
#define NETWORK_ERROR_CAPACITY      (NETWORK_ERROR_BASE + 6)

#define NETWORK_SEND_COMPLETE_STRING -34768

// structures defined by implementation
typedef struct _network_server_t network_server_t;
typedef struct _network_connection_t network_connection_t;

/**
 * Called when a new connection has been initiated.
 * Network can already be sent to but lines can only be received after this call succeeds.
 * handler_reference should be set to something that can be used to identify the connection;
 * the connection reference should be saved.
 * constructor_reference is provided as configured in network_handler_t.
 */
typedef error_t (*new_connection_f)(network_connection_t *connection, void **handler_reference, void *constructor_reference);

/**
 * Called for each line received from the network.
 */
typedef void (*on_line_received_f)(void *handler_reference, char *line, int length);

/**
 * Called by close_network_connection when network connection is closing.
 * Connection may already have been partially closed.
 * This is the last call using handler_reference from network side.
 */
typedef void (*on_connection_closing_f)(void *handler_reference);

typedef struct {
    new_connection_f new_connection;
    void *new_connection_constructor_reference;
    on_line_received_f on_line_received;
    on_connection_closing_f on_connection_closing;
} network_handler_t;

#define INTERFACE_LOCAL "localhost"

typedef struct {
    bool enable_ipv6;
    char *interface;
    uint16_t port;
} network_server_config_t;

error_t create_network_server(network_server_t **server, network_server_config_t *config, network_handler_t handler);
bool destroy_network_server(network_server_t *server);

error_t send_to_network(network_connection_t *connection, char *content, int length);
void close_network_connection(network_connection_t *connection);

#endif
