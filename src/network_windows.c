/* The goal of this file is to establish compatibility with Windows operating systems.
 *
 * This file is based on API information published by Microsoft under CC-BY 4.0 and MIT licenses at:
 *
 *   https://github.com/MicrosoftDocs/sdk-api
 *   revision 5da3012685fee3b1dbbefe7fa1f9a9935b9fa14e (2 Aug 2024)
 *   see repository at specified revision for detailed license information
 *
 * Official API documentation omits headers and thus low-level type information. Missing information has
 * been substituted in reference to headers distributed as part of wine which are published under terms of
 * LGPL 2.1:
 *
 *   https://github.com/wine-mirror/wine/blob/master/include/
 *
 * This file itself remains published under MIT license. If one of the API reference sources requires a more
 * restrictive license to be put into effect, the respective license shall take precedence with closely limited effect
 * in accordance to the right to sublicense MIT-licensed works. This will mainly affect binary distributions and
 * other code that might be based upon this file. To avoid licensing issues and to avoid taking over a potentially
 * wrong implementation, it is strongly recommended not to use this file for reference in other projects; follow
 * the original API docs on your own instead.
 */

#include <stddef.h>
#include <stdlib.h>

#include <stdio.h>

// Windows APIs
#include <winsock2.h>
#include <iphlpapi.h>

#include "network.h"

#include "utils.h"

#define MAJOR_WINSOCK_VERSION 2
#define MINOR_WINSOCK_VERSION 2

#define MAX_ADDRESS_RESULT_BUFFER_SIZE (1 * 1024 * 1024) /* 1MB should be enough for everyone */
#define ADDRESS_STRING_BUFFER_SIZE 40 /* IPv6: 8x 4 hex chars + 7 dividers + 1 NULL terminator */

error_t initialize_os_network_apis() {
    unsigned long res = 0;

    WSADATA wsadata = {0,};

    uint16_t requested_winsock_version = (MAJOR_WINSOCK_VERSION << 8) | MINOR_WINSOCK_VERSION;
    res = WSAStartup(requested_winsock_version, &wsadata);
    if (res) {
        printf("[XPRC] WSAStartup failed, res=%lu\r\n", res);
        return ERROR_UNSPECIFIC;
    }

    return ERROR_NONE;
}

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
    // Microsoft API docs:
    //   docs/sdk-api-src/content/iphlpapi/nf-iphlpapi-getadaptersaddresses.md
    //   docs/sdk-api-src/content/iptypes/ns-iptypes-ip_adapter_addresses_lh.md
    //   docs/sdk-api-src/content/winsock2/nf-winsock2-wsaaddresstostringa.md

    void *result_buffer = NULL;
    list_t *out = NULL;
    unsigned long res = 0;

    // GetAdaptersAddresses needs a fully pre-allocated result buffer. Microsoft recommends to start with 15kB for a
    // first attempt. Due to the high number of network interfaces usually encountered these days we start with 64kB
    // instead.
    long unsigned int result_buffer_size = 64 * 1024;
    while (true) {
        if (result_buffer) {
            free(result_buffer);
            result_buffer = NULL;
        }

        long unsigned int original_result_buffer_size = result_buffer_size;
        result_buffer = zalloc(result_buffer_size);
        if (!result_buffer) {
            goto error;
        }

        res = GetAdaptersAddresses(
                /* Family           */ include_ipv6 ? AF_UNSPEC : AF_INET,
                /* Flags            */ GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_SKIP_FRIENDLY_NAME |
                                       GAA_FLAG_INCLUDE_ALL_INTERFACES,
                /* (Reserved)       */ NULL,
                /* AdapterAddresses */ result_buffer,
                /* SizePointer      */ &result_buffer_size
        );

        // exit loop if successful
        if (res == ERROR_SUCCESS) {
            break;
        } else if (res == ERROR_BUFFER_OVERFLOW) {
            // Windows indicates that our buffer is too small and has modified result_buffer_size to indicate
            // the suggested size for the next attempt.
            // We only retry if that requested size is actually larger than before and while it is within limit.
            if ((result_buffer_size <= original_result_buffer_size) || (result_buffer_size > MAX_ADDRESS_RESULT_BUFFER_SIZE)) {
                printf(
                    "[XPRC] GetAdaptersAddresses unreasonable ERROR_BUFFER_OVERFLOW; result_buffer_size=%lu, original_result_buffer_size=%lu\r\n",
                    res,
                    result_buffer_size,
                    original_result_buffer_size
                );
                goto error;
            }

            continue;
        }

        // something went wrong; abort
        printf("[XPRC] GetAdaptersAddresses generic error res=%lu\r\n", res);
        goto error;
    }

    out = create_list();
    if (!out) {
        goto error;
    }

    IP_ADAPTER_ADDRESSES *item = (IP_ADAPTER_ADDRESSES*) result_buffer;
    while (item) {
        IP_ADAPTER_UNICAST_ADDRESS_XP *unicast_item = item->FirstUnicastAddress;
        while (unicast_item) {
            SOCKET_ADDRESS address_container = unicast_item->Address;

            // WSAAddressToString expects the length to be indicated for either regular chars or WCHAR (Unicode)
            // but has no parameter to indicate what we actually meant to provide nor does it tell us what it
            // wrote afterwards, duh...
            // To be on the safe side we reserve 4 bytes for every character we want to receive which would fit UTF32.
            // We may end up copying garbage to the output list in the end but it should gracefully fail by producing
            // something users can't select as it's not a parseable address (probably would just show up as an empty
            // or severely shortened string).
            long unsigned int address_string_length = ADDRESS_STRING_BUFFER_SIZE;
            char *address_string = zalloc(address_string_length * 4);
            if (!address_string) {
                printf("[XPRC] failed to allocate address_string\r\n");
                goto error;
            }

            res = WSAAddressToString(
                    /* lpsaAddress             */ address_container.lpSockaddr,
                    /* dwAddressLength         */ address_container.iSockaddrLength,
                    /* lpProtocolInfo          */ NULL,
                    /* lpszAddressString       */ address_string,
                    /* lpdwAddressStringLength */ &address_string_length
            );

            if (res) {
                // error
                res = WSAGetLastError();
                if (res == WSANOTINITIALISED) {
                    printf("[XPRC] WSAAddressToString error decoded: WSANOTINITIALISED\r\n");
                } else if (res == WSAENOBUFS) {
                    printf("[XPRC] WSAAddressToString error decoded: WSAENOBUFS\r\n");
                } else if (res == WSAEINVAL) {
                    printf("[XPRC] WSAAddressToString error decoded: WSAEINVAL\r\n");
                } else if (res == WSAEFAULT) {
                    printf("[XPRC] WSAAddressToString error decoded: WSAEFAULT\r\n");
                } else {
                    printf("[XPRC] WSAAddressToString unknown error: %lu\r\n", res);
                }

                free(address_string);
                address_string = NULL;
            } else {
                // no error
                char *copy = copy_partial_string(address_string, address_string_length);
                free(address_string);
                address_string = NULL;

                if (!copy) {
                    printf("[XPRC] WSAAddressToString failed copy\r\n");
                    goto error;
                }

                if (!list_append(out, copy)) {
                    printf("[XPRC] WSAAddressToString failed append\r\n");
                    free(copy);
                    goto error;
                }
            }

            unicast_item = unicast_item->Next;
        }

        item = item->Next;
    }

    free(result_buffer);

    return out;

error:
    if (out) {
        destroy_list(out, free);
    }

    if (result_buffer) {
        free(result_buffer);
    }

    printf("[XPRC] get_network_interfaces aborted\r\n");

    return NULL;
}
