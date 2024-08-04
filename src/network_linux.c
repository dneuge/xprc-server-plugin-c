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
                printf("[XPRC] get_network_interfaces: IP address is not recognized as valid, skipping: \"%s\"\r\n", ntop_buffer);
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

error_t initialize_os_network_apis() {
    // we do not need to initialize anything on Linux
    return ERROR_NONE;
}
