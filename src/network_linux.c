#include <stdlib.h>
#include <string.h>

#include "utils.h"

list_t* get_network_interfaces(bool include_ipv6) {
    struct ifaddrs *sys_interfaces = NULL;

    int res = getifaddrs(&sys_interfaces);
    if (res != 0) {
        return NULL;
    }

    char *ntop_buffer = zalloc(NTOP_BUFFER_SIZE);
    if (!ntop_buffer) {
        goto error;
    }

    list_t *out = create_list();
    if (!out) {
        goto error;
    }

    struct ifaddrs *sys_interface = sys_interfaces;
    while (sys_interface) {
        char *name = NULL;

        if (!sys_interface->ifa_addr) {
            sys_interface = sys_interface->ifa_next;
            continue;
        }

        sa_family_t family = sys_interface->ifa_addr->sa_family;
        bool should_include = (family == AF_INET) || (include_ipv6 && (family == AF_INET6));
        if (!should_include) {
            sys_interface = sys_interface->ifa_next;
            continue;
        }

        memset(ntop_buffer, 0, NTOP_BUFFER_SIZE);

        void *sys_addr = (family == AF_INET6) ? (void*) &((struct sockaddr_in6*) sys_interface->ifa_addr)->sin6_addr
                                              : (void*) &((struct sockaddr_in*) sys_interface->ifa_addr)->sin_addr;
        if (inet_ntop(family, sys_addr, ntop_buffer, NTOP_BUFFER_SIZE - 1)) {
            if (!is_ip_address(ntop_buffer)) {
                // avoid errors in caller; we are supposed to only return what we understand as IP addresses ourselves
                RCLOG_WARN("get_network_interfaces: IP address is not recognized as valid, skipping: \"%s\"", ntop_buffer);
            } else {
                // valid IP address
                name = copy_string(ntop_buffer);
                if (name) {
                    if (!list_append(out, name)) {
                        free(name);
                        goto error;
                    }
                }
            }
        }

        sys_interface = sys_interface->ifa_next;
    }

    free(ntop_buffer);
    freeifaddrs(sys_interfaces);

    return out;

error:
    if (sys_interfaces) {
        freeifaddrs(sys_interfaces);
    }

    if (out) {
        destroy_list(out, free);
    }

    if (ntop_buffer) {
        free(ntop_buffer);
    }

    return NULL;
}

static struct sockaddr* create_address_ipv6(network_server_config_t *config) {
    struct sockaddr_in6 *address = malloc(sizeof(struct sockaddr_in6));
    if (!address) {
        return NULL;
    }

    memset(address, 0, sizeof(struct sockaddr_in6));

    address->sin6_family = AF_INET6;
    address->sin6_port = htons(config->port);

    if (!config->interface_address) {
        address->sin6_addr = in6addr_any;
    } else if (!strcmp(config->interface_address, INTERFACE_LOCAL)) {
        address->sin6_addr = in6addr_loopback;
    } else if (!is_ipv6_address(config->interface_address)) {
        RCLOG_WARN("Not a valid IPv6 address for interface: \"%s\"", config->interface_address);
        free(address);
        return NULL;
    } else {
        // FIXME: interface-local addresses (fe80::) need sin6_scope_id to be set as well, otherwise the socket fails to bind
        int res = inet_pton(AF_INET6, config->interface_address, &address->sin6_addr);
        if (res != 1) {
            RCLOG_WARN("unable to resolve interface \"%s\" (system error %d)", config->interface_address, res);
            free(address);
            return NULL;
        }
    }

    return (struct sockaddr*) address;
}

static int close_socket(int sd) {
    return close(sd);
}

error_t initialize_os_network_apis() {
    // we do not need to initialize anything on Linux
    return ERROR_NONE;
}
