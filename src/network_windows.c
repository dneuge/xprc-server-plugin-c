#include <stddef.h>

#include "network.h"

error_t create_network_server(network_server_t **server, network_server_config_t *config, network_handler_t handler) {
    // FIXME: implement for Windows
    return ERROR_UNSPECIFIC;
}

bool destroy_network_server(network_server_t *server) {
    // FIXME: implement for Windows
    return true;
}

error_t send_to_network(network_connection_t *connection, char *content, int length) {
    // FIXME: implement for Windows
    return ERROR_UNSPECIFIC;
}

void close_network_connection(network_connection_t *connection) {
    // FIXME: implement for Windows
}

list_t* get_network_interfaces(bool include_ipv6) {
    // FIXME: implement for Windows
    return NULL;
}
