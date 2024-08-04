/* In an effort to share as much code as possible across operating systems, this file contains some early includes and
 * aliases to establish compatibility with Windows operating systems. The code is primarily developed on/for Linux
 * but Windows network APIs appear to be similar enough to facilitate rather low-effort portability.
 *
 * Necessary adaptions are based on API information published by Microsoft under CC-BY 4.0 and MIT licenses at:
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

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NEED_C11_THREADS_WRAPPER
#include <threads.h>
#else
#include <c11/threads.h>
#endif

#include "network.h"

#ifdef TARGET_LINUX
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <unistd.h>

static const int SOCKOPT_ENABLE_VALUE = 1;
#define SOCKOPT_ENABLE_SIZE sizeof(int)
#elif TARGET_WINDOWS
// Windows API
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>

// Microsoft API docs:
// [sdk-api] docs/sdk-api-src/content/winsock2/nf-winsock2-shutdown.md

#define SHUT_RD SD_RECEIVE

static const char SOCKOPT_ENABLE_VALUE = 1;
#define SOCKOPT_ENABLE_SIZE sizeof(char)
#else
#error "OS-specific early parts of network.c are missing; target OS is not supported"
#endif

static bool parse_ipv4_segment(uint8_t *out, char *address, int start, int endExcl) {
    int segment_length = endExcl - start;
    if ((segment_length < 1) || (segment_length > 3)) {
        return false;
    }

    int segment = atoi(&(address[start]));

    if ((segment < 0) || (segment > 255)) {
        return false;
    }

    *out = (uint8_t) segment;

    return true;
}

static bool parse_ipv4_address(uint8_t segments[4], char *address) {
    uint8_t out[4] = {0,};

    if (!address) {
        return false;
    }

    int segmentStart = 0;
    int segmentsComplete = 0;
    int i=0;
    bool complete = false;
    while (segmentsComplete < 4) {
        char ch = address[i];
        if (!ch || (ch == '.')) {
            // we can prematurely increment segmentsComplete because we discard it anyway if parsing fails
            if (!parse_ipv4_segment(&out[segmentsComplete++], address, segmentStart, i)) {
                return false;
            }

            segmentStart = i + 1;

            if (!ch) {
                complete = true;
                break;
            }
        } else if ((ch < '0') || (ch > '9')) {
            // not a digit
            return false;
        }

        i++;
    }

    if (!complete || (segmentsComplete != 4)) {
        return false;
    }

    for (int j=0; j<4; j++) {
        segments[j] = out[j];
    }

    return true;
}

bool is_ipv4_address(char *address) {
    uint8_t segments[4] = {0,};
    return parse_ipv4_address(segments, address);
}

static bool parse_ipv6_segment(uint16_t *out, char *s, int length) {
    if ((length < 1) || (length > 4)) {
        return false;
    }

    uint16_t segment = 0;
    for (int i=0; i<length; i++) {
        char ch = s[i];
        if (ch >= '0' && ch <= '9') {
            segment = (segment << 4) + (ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            segment = (segment << 4) + (ch - 'a');
        } else if (ch >= 'A' && ch <= 'F') {
            segment = (segment << 4) + (ch - 'A');
        } else {
            return false;
        }
    }

    *out = segment;

    return true;
}

static bool parse_ipv6_address(uint16_t segments[8], char *s) {
    uint16_t front_segments[8] = {0,};
    int num_front_segments = 0;
    uint16_t rear_segments[8] = {0,};
    int num_rear_segments = 0;
    bool expand = false;
    int i = 0;
    int segment_start = 0;

    if (!s || !segments) {
        return false;
    }

    // Having the expansion placeholder at the beginning or even standalone is hard to handle
    // without creating lots of other special cases in the parser, so it's better to treat it
    // separately.
    if (!strncmp(s, "::", 2)) {
        if (strlen(s) == 2) {
            // we have only the placeholder, nothing else
            num_front_segments = 8; // array is fully zeroed, just copy that and we are done
            goto finalize;
        } else {
            // string only starts with the placeholder but continues after that
            expand = true;
            i = 2;
            segment_start = 2;
        }
    }

    bool complete = false;
    while (num_front_segments < 8 && num_rear_segments < 8) {
        char ch = s[i++];
        char lookahead1 = ch ? s[i] : 0; // i has already been incremented for next iteration
        char lookahead2 = lookahead1 ? s[i+1] : 0;

        if (((ch >= '0') && (ch <= '9')) || ((ch >= 'a') && (ch <= 'f')) || ((ch >= 'A') && (ch <= 'F'))) {
            // valid character, will be processed later
            continue;
        } else if (!ch || (ch == ':')) {
            // premature num_..._segments increment is okay because with an invalid segment/parsing error we will never
            // use that wrongly incremented counter anyway
            uint16_t *dest = !expand ? &front_segments[num_front_segments++] : &rear_segments[num_rear_segments++];
            int length = i - segment_start - 1; // i has already been incremented, so we need to subtract one more
            if (!parse_ipv6_segment(dest, &s[segment_start], length)) {
                // segment was empty or had some other syntax error => abort
                return false;
            }

            segment_start = i; // i has already been incremented

            if (lookahead1 == ':') {
                // expansion placeholder
                if (expand) {
                    // the placeholder was already used previously => syntax error, abort
                    return false;
                }
                expand = true;

                // skip one character
                i++;
                segment_start++;

                if (!lookahead2) {
                    // end of string - quit early to avoid having to handle zero length special cases on next iteration
                    complete = true;
                    break;
                }
            } else if (!lookahead1 && (ch != ':')) {
                // end of string and address does not end in colon
                // if the address would end in a colon, continue to iterate and fail with zero segment next
                complete = true;
                break;
            }
        }
    }

    if (!complete) {
        return false;
    }

finalize:
    if (num_front_segments + num_rear_segments + (expand ? 1 : 0) > 8) {
        return false;
    }
    int num_fill = 8 - num_front_segments - num_rear_segments;
    for (int j=0; j<num_front_segments; j++) {
        segments[j] = front_segments[j];
    }
    for (int j=0; j<num_fill; j++) {
        segments[num_front_segments + j] = 0;
    }
    for (int j=0; j<num_rear_segments; j++) {
        segments[num_front_segments + num_fill + j] = rear_segments[j];
    }
    return true;
}

bool is_ipv6_address(char *address) {
    uint16_t segments[8] = {0,};
    return parse_ipv6_address(segments, address);
}

typedef int32_t seg2i32_f(void *segments, int segment_index);

static int cmp_address(char *a, char *b, void *segments_a, void *segments_b, seg2i32_f read_segment, bool parsed_a, bool parsed_b, int num_segments) {
    if (parsed_a && parsed_b) {
        for (int i=0; i<num_segments; i++) {
            int32_t res = read_segment(segments_a, i) - read_segment(segments_b, i);
            if (res != 0) {
                return res;
            }
        }
        return 0;
    } else if (!parsed_a) {
        // a is not an address
        if (!parsed_b) {
            // neither is an address
            if (!a) {
                // a is NULL, so we cannot call strcmp - NULL should go first
                return b ? -1 : 0;
            } else if (!b) {
                // b is NULL, a is not, so a is greater
                return 1;
            }

            return strcmp(a, b);
        }

        return -1;
    } else {
        // a is an address, b is not
        return 1;
    }
}

static int32_t read_ipv4_segment_int32(void *segments, int i) {
    return (int32_t) ((uint8_t*) segments)[i];
}

int cmp_ipv4_address(char *a, char *b) {
    uint8_t segments_a[4] = {0,};
    uint8_t segments_b[4] = {0,};

    bool parsed_a = parse_ipv4_address(segments_a, a);
    bool parsed_b = parse_ipv4_address(segments_b, b);

    return cmp_address(a, b, segments_a, segments_b, read_ipv4_segment_int32, parsed_a, parsed_b, 4);
}

static int32_t read_ipv6_segment_int32(void *segments, int i) {
    return (int32_t) ((uint16_t*) segments)[i];
}

int cmp_ipv6_address(char *a, char *b) {
    uint16_t segments_a[8] = {0,};
    uint16_t segments_b[8] = {0,};

    bool parsed_a = parse_ipv6_address(segments_a, a);
    bool parsed_b = parse_ipv6_address(segments_b, b);

    return cmp_address(a, b, segments_a, segments_b, read_ipv6_segment_int32, parsed_a, parsed_b, 8);
}

int cmp_ip_address(char *a, char *b) {
    bool a4 = is_ipv4_address(a);
    bool b4 = is_ipv4_address(b);
    bool a6 = is_ipv6_address(a);
    bool b6 = is_ipv6_address(b);

    if (a4 && b4) {
        // both are IPv4 addresses, compare them
        return cmp_ipv4_address(a,b);
    } else if (a6 && b6) {
        // both are IPv6 addresses, compare them
        return cmp_ipv6_address(a,b);
    } else if (a4 && b6) {
        // a is IPv4, b is IPv6 => IPv4 (a) goes first
        return -1;
    } else if (b4 && a6) {
        // a is IPv6, b is IPv4 => IPv4 (b) goes first
        return 1;
    } else if ((a4 || a6) && !(b4 || b6)) {
        // a is an IP address, b is not => text (b) goes first
        return 1;
    } else if (!(a4 || a6) && (b4 || b6)) {
        // b is an IP address, a is not => text (a) goes first
        return -1;
    } else {
        // neither is an IP address, compare as text
        if (!a) {
            // a is NULL, so we cannot call strcmp - NULL should go first
            return b ? -1 : 0;
        } else if (!b) {
            // b is NULL, a is not, so a is greater
            return 1;
        }

        return strcmp(a, b);
    }
}

bool is_ip_address(char *address) {
    return is_ipv4_address(address) || is_ipv6_address(address);
}

#define MAX_CONNECTIONS 1024
#define CONNECTION_BACKLOG 10 /* maximum number of connections waiting to be accepted */
#define SERVER_MAX_CONSECUTIVE_ERRORS 2000 /* server thread will terminate after these number of consecutive errors */
#define SEND_BUFFER_SIZE (2 * 1024 * 1024)
#define SEND_CHUNK_SIZE 512
#define RECEIVE_MAX_LINE_LENGTH (64 * 1024)
#define RECEIVE_CHUNK_SIZE 512
#define SEND_CHECK_INTERVAL_MICROSECONDS 500000

#define IPV6_NTOP_MIN_BUFFER_SIZE 39 /* fully expanded IPv6 address */
#define TARGET_NTOP_MIN_BUFFER_SIZE MAX(INET_ADDRSTRLEN, INET6_ADDRSTRLEN) /* address required by API definition */
#define NTOP_BUFFER_SIZE ((MAX(TARGET_NTOP_MIN_BUFFER_SIZE, IPV6_NTOP_MIN_BUFFER_SIZE)) + 1)

typedef struct _network_connection_t {
    bool in_use; // connections are reused by server; no lock needed - only set true by server thread, reset to false when closed
    int sd; // socket descriptor
    bool closing; // multiple threads can ask for connections to be closed incl. connections themselves which makes them unjoinable; instead we only mark them as "closing" while keeping them "in use" and wait for a maintenance thread to perform the actual shutdown
    bool socket_closed;
    network_server_t *server;
    void *handler_reference;

    thrd_t send_thread;
    bool joined_send_thread;
    char *send_ringbuffer;
    int send_read_pos;
    int send_write_pos;
    mtx_t send_mutex;
    bool has_send_mutex;
    cnd_t send_wait;
    bool has_send_wait;

    thrd_t recv_thread;
    bool joined_recv_thread;

    bool shutdown;
} network_connection_t;

typedef struct _network_server_t {
    int ssd; // server socket descriptor

    thrd_t server_thread;
    bool server_thread_stopped;
    bool shutdown;

    thrd_t maintenance_thread;

    network_handler_t handler;

    network_connection_t connections[MAX_CONNECTIONS];
} network_server_t;

const struct timespec MAINTENANCE_INTERVAL = {
        .tv_sec = 2,
        .tv_nsec = 0,
};

static inline int _min(int a, int b) {
    return (a < b) ? a : b;
}

static int time_from_now(struct timespec *out, uint32_t microseconds) {
    struct timespec timeout;
    if (!timespec_get(&timeout, TIME_UTC)) {
        return ERROR_UNSPECIFIC;
    }

    time_t seconds_to_add = microseconds / 1000000;
    long nanoseconds_to_add = (microseconds % 1000000) * 1000;

    // add to timestamp
    timeout.tv_sec += seconds_to_add;
    timeout.tv_nsec += nanoseconds_to_add;

    // overflow to seconds
    timeout.tv_sec += timeout.tv_nsec / 1000000000;
    timeout.tv_nsec = timeout.tv_nsec % 1000000000;

    *out = timeout;

    return ERROR_NONE;
}

static int run_send_thread(void *arg) {
    network_connection_t *connection = arg;
    struct timespec wait_until = {0};
    bool holds_lock = false;

    if (mtx_lock(&connection->send_mutex) != thrd_success) {
        printf("failed to get initial lock of mutex in send thread\n"); // TODO: log
    } else {
        holds_lock = true;
    }

    while (holds_lock && !connection->shutdown) {
        if (connection->send_read_pos == connection->send_write_pos) {
            // everything has been sent (pointers are equal), go to sleep
            if (time_from_now(&wait_until, SEND_CHECK_INTERVAL_MICROSECONDS) != ERROR_NONE) {
                printf("unable to calculate time to wait in send thread\n"); // TODO: log
                break;
            }

            int res = cnd_timedwait(&connection->send_wait, &connection->send_mutex, &wait_until);
            if (res != thrd_success && res != thrd_timedout) {
                printf("condition failed to wait on send thread\n"); // TODO: log
                holds_lock = false;
                break;
            }

            continue;
        }

        // there is something to be sent
        size_t pending = 0;
        if (connection->send_read_pos < connection->send_write_pos) {
            // read pointer is behind write pointer, we can send everything up until there
            pending = connection->send_write_pos - connection->send_read_pos;
        } else {
            // read pointer is ahead of write pointer, send only until end of buffer for now
            pending = SEND_BUFFER_SIZE - connection->send_read_pos;
        }

        int chunk_size = _min(pending, SEND_CHUNK_SIZE);

        // release lock as we will perform long-running calls now
        mtx_unlock(&connection->send_mutex);
        holds_lock = false;

        ssize_t sent = send(connection->sd, connection->send_ringbuffer + connection->send_read_pos, chunk_size, 0);
        if (sent <= 0) {
            printf("failed to send\n"); // TODO: log
            break;
        }

        // we need to regain the lock to continue
        if (mtx_lock(&connection->send_mutex) != thrd_success) {
            printf("failed to regain lock on mutex in send thread\n"); // TODO: log
            break;
        }
        holds_lock = true;

        // forward pointer, wrap if needed
        connection->send_read_pos += sent;
        if (connection->send_read_pos >= SEND_BUFFER_SIZE) {
            connection->send_read_pos = 0;
        }
    }

    if (holds_lock) {
        mtx_unlock(&connection->send_mutex);
    }

    printf("send thread terminates connection\n"); // TODO: log
    close_network_connection(connection);

    return 0;
}

static int run_receive_thread(void *arg) {
    network_connection_t *connection = arg;

    char line_buffer[RECEIVE_MAX_LINE_LENGTH] = {0};
    int line_write_offset = 0;
    char chunk_buffer[RECEIVE_CHUNK_SIZE] = {0};
    ssize_t received;

    while (!(connection->shutdown || connection->closing)) {
        received = recv(connection->sd, chunk_buffer, RECEIVE_CHUNK_SIZE, 0);
        if (received == 0) {
            // connection was closed
            break;
        } else if (received < 0) {
            printf("error while reading from connection\n"); // TODO: log
            break;
        }

        bool abort = false;
        for (int i=0; i<received && !connection->closing; i++) { // reason for closing check: stop processing content as soon as we want to close the connection
            char ch = chunk_buffer[i];
            if (ch == '\n' || ch == '\r') {
                if (line_write_offset == 0) {
                    // empty line, ignore
                    continue;
                }

                // line is complete
                connection->server->handler.on_line_received(connection->handler_reference, line_buffer, line_write_offset);

                line_write_offset = 0;
            } else {
                // transfer character to line
                line_buffer[line_write_offset++] = ch;
                if (line_write_offset >= RECEIVE_MAX_LINE_LENGTH) {
                    // excessively long lines cannot be handled, connection must be terminated
                    printf("receive buffer exceeded\n"); // TODO: log
                    abort = true;
                    break;
                }
            }
        }

        if (abort) {
            break;
        }
    }

    printf("receive thread terminates connection\n"); // TODO: log
    close_network_connection(connection);

    return 0;
}

static bool shutdown_network_connection(network_connection_t *connection) {
    int res;
    bool success = true;

    if (!connection->in_use) {
        printf("connection is not in use\n"); // TODO: log
        return true;
    }

    if (connection->handler_reference) {
        connection->server->handler.on_connection_closing(connection->handler_reference);
        connection->handler_reference = NULL;
    }

    connection->shutdown = true;

    if (connection->has_send_wait) {
        cnd_broadcast(&connection->send_wait);
    }

    if (!connection->socket_closed) {
        shutdown(connection->sd, SHUT_RD); // QUESTION: close write channel as well?
        close(connection->sd);
        connection->socket_closed = true;
    }

    if (!connection->joined_recv_thread) {
        if (thrd_join(connection->recv_thread, &res) == thrd_success) {
            connection->joined_recv_thread = true;
        } else {
            printf("failed to join client receive thread\n"); // TODO: log
            success = false;
        }
    }

    if (!connection->joined_send_thread) {
        if (thrd_join(connection->send_thread, &res) == thrd_success) {
            connection->joined_send_thread = true;
        } else {
            printf("failed to join client send thread\n"); // TODO: log
            success = false;
        }
    }

    if (!success) {
        printf("connection could not be terminated\n"); // TODO: log with high severity
        return false; // connection is a zombie and must remain "in use"
    }

    if (connection->has_send_wait) {
        cnd_destroy(&connection->send_wait);
        connection->has_send_wait = false;
    }

    if (connection->has_send_mutex) {
        mtx_destroy(&connection->send_mutex);
        connection->has_send_mutex = false;
    }

    if (connection->send_ringbuffer) {
        free(connection->send_ringbuffer);
        connection->send_ringbuffer = NULL;
    }

    return true;
}

static int run_maintenance_thread(void *arg) {
    network_server_t *server = arg;

    while (!(server->shutdown && server->server_thread_stopped)) {
        for (int i=0; i<MAX_CONNECTIONS; i++) {
            network_connection_t *connection = &(server->connections[i]);

            if (!(connection->in_use && connection->closing)) {
                // nothing to do on that connection
                continue;
            }

            if (!shutdown_network_connection(connection)) {
                // failed to shut down, try again later
                continue;
            }

            // shutdown successful, mark unused
            connection->in_use = false;
        }

        thrd_sleep(&MAINTENANCE_INTERVAL, NULL);
    }

    // if we got here, server was requested to shut down and server thread has terminated,
    // so no new connections will be accepted - we need to shutdown all remaining connections now
    bool success = true;
    for (int i=0; i<MAX_CONNECTIONS; i++) {
        network_connection_t *connection = &(server->connections[i]);
        if (!connection->in_use) {
            continue;
        }

        if (shutdown_network_connection(connection)) {
            connection->in_use = false;
        } else {
            success = false;
        }
    }

    return success ? ERROR_NONE : ERROR_UNSPECIFIC;
}

static int run_server_thread(void *arg) {
    network_server_t *server = arg;

    unsigned int consecutive_errors = 0;

    while (!server->shutdown && (consecutive_errors < SERVER_MAX_CONSECUTIVE_ERRORS)) {
        int sd = accept(server->ssd, NULL, NULL);
        if (sd < 0) {
            printf("accept failed\n"); // TODO: log
            consecutive_errors++;
            continue;
        }

        // find a free connection instance
        int connection_id = -1;
        for (int i=0; i<MAX_CONNECTIONS; i++) {
            if (!server->connections[i].in_use) {
                connection_id = i;
                break;
            }
        }

        // close socket if we have reached maximum connections
        // NOTE: this does *not* count as a reason to shut down, so consecutive_errors is *not* incremented
        if (connection_id < 0) {
            printf("maximum number of connections reached\n"); // TODO: log
            close(sd);
            continue;
        }

        network_connection_t *connection = &(server->connections[connection_id]);
        memset(connection, 0, sizeof(network_connection_t));
        connection->in_use = true;
        connection->sd = sd;
        connection->server = server;

        if (mtx_init(&connection->send_mutex, mtx_plain) != thrd_success) {
            printf("failed to initialize send mutex\n"); // TODO: log
            consecutive_errors++;
            close_network_connection(connection);
            continue;
        }
        connection->has_send_mutex = true;

        if (cnd_init(&connection->send_wait) != thrd_success) {
            printf("failed to initialize send condition\n"); // TODO: log
            consecutive_errors++;
            close_network_connection(connection);
            continue;
        }
        connection->has_send_wait = true;

        connection->send_ringbuffer = malloc(SEND_BUFFER_SIZE);
        if (!connection->send_ringbuffer) {
            printf("failed to allocate send ring buffer\n"); // TODO: log
            consecutive_errors++;
            close_network_connection(connection);
            continue;
        }

        if (thrd_create(&connection->send_thread, run_send_thread, connection) != thrd_success) {
            printf("failed to spawn send thread\n"); // TODO: log
            consecutive_errors++;
            close_network_connection(connection);
            continue;
        }

        if (thrd_create(&connection->recv_thread, run_receive_thread, connection) != thrd_success) {
            printf("failed to spawn receive thread\n"); // TODO: log
            consecutive_errors++;
            close_network_connection(connection);
            continue;
        }

        int res = server->handler.new_connection(connection, &connection->handler_reference, server->handler.new_connection_constructor_reference);
        if (res != ERROR_NONE) {
            printf("handler failed to create connection: %d\n", res); // TODO: log
            consecutive_errors++;
            close_network_connection(connection);
            continue;
        }

        consecutive_errors = 0;
    }

    printf("server thread shutting down\n"); // TODO: log
    close(server->ssd);

    server->server_thread_stopped = true;

    return 0;
}

static struct sockaddr* create_address_ipv4(network_server_config_t *config) {
    struct sockaddr_in *address = malloc(sizeof(struct sockaddr_in));
    if (!address) {
        return NULL;
    }

    memset(address, 0, sizeof(struct sockaddr_in));

    address->sin_family = AF_INET;
    address->sin_port = htons(config->port);

    if (!config->interface_address) {
        address->sin_addr.s_addr = INADDR_ANY;
    } else if (!strcmp(config->interface_address, INTERFACE_LOCAL)) {
        address->sin_addr.s_addr = INADDR_LOOPBACK;
    } else {
        // TODO: support selection of specific interface to bind to
        printf("unable to resolve interface \"%s\"\n", config->interface_address); // TODO: log
        free(address);
        return NULL;
    }

    return (struct sockaddr*) address;
}

// structure for IPv6 addresses is different on Windows and Linux, so we need to implement it OS-specific
// Microsoft API docs:
// [sdk-api] docs/sdk-api-src/content/ws2ipdef/ns-ws2ipdef-sockaddr_in6_*.md
static struct sockaddr* create_address_ipv6(network_server_config_t *config);

static struct sockaddr* create_address(network_server_config_t *config) {
    if (config->enable_ipv6) {
        return create_address_ipv6(config);
    } else {
        return create_address_ipv4(config);
    }
}

error_t create_network_server(network_server_t **server, network_server_config_t *config, network_handler_t handler) {
    int res;

    struct sockaddr *address = create_address(config);
    socklen_t address_size = config->enable_ipv6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
    if (!address) {
        printf("failed to create server address\n"); // TODO: log
        return NETWORK_ERROR_BAD_ADDRESS;
    }

    *server = malloc(sizeof(network_server_t));
    if (!*server) {
        free(address);
        return ERROR_MEMORY_ALLOCATION;
    }

    memset(*server, 0, sizeof(network_server_t));
    (*server)->handler = handler;

    int family = config->enable_ipv6 ? AF_INET6 : AF_INET;
    (*server)->ssd = socket(family, SOCK_STREAM, 0);
    if ((*server)->ssd == -1) {
        free(address);
        free(*server);
        *server = NULL;
        return NETWORK_ERROR_NO_SOCKET;
    }

    res = setsockopt((*server)->ssd, SOL_SOCKET, SO_REUSEADDR, &SOCKOPT_ENABLE_VALUE, SOCKOPT_ENABLE_SIZE);
    if (res) {
        printf("failed to enable REUSEADDR on socket: %d, errno %d\n", res, errno); // TODO: log
    }

    res = setsockopt((*server)->ssd, SOL_SOCKET, SO_KEEPALIVE, &SOCKOPT_ENABLE_VALUE, SOCKOPT_ENABLE_SIZE);
    if (res) {
        printf("failed to enable KEEPALIVE on socket: %d, errno %d\n", res, errno); // TODO: log
    }

    res = bind((*server)->ssd, (struct sockaddr*) address, address_size);
    free(address);
    if (res) {
        printf("failed to bind on requested address: %d, errno %d\n", res, errno); // TODO: log
        close((*server)->ssd);
        free(*server);
        *server = NULL;
        return NETWORK_ERROR_BIND_FAILED;
    }

    if (listen((*server)->ssd, CONNECTION_BACKLOG)) {
        printf("failed to listen on server socket\n"); // TODO: log
        close((*server)->ssd);
        free(*server);
        *server = NULL;
        return NETWORK_ERROR_LISTEN_FAILED;
    }

    if (thrd_create(&(*server)->server_thread, run_server_thread, *server) != thrd_success) {
        printf("failed to spawn server thread\n"); // TODO: log
        close((*server)->ssd);
        free(*server);
        *server = NULL;
        return NETWORK_ERROR_THREAD_FAILED;
    }

    if (thrd_create(&(*server)->maintenance_thread, run_maintenance_thread, *server) != thrd_success) {
        printf("failed to spawn maintenance thread\n");
        (*server)->shutdown = true;
        shutdown((*server)->ssd, SHUT_RD);
        close((*server)->ssd);
        if (thrd_join((*server)->server_thread, &res) != thrd_success) {
            printf("failed to rejoin server thread during failed construction cleanup\n"); // TODO: log
            return NETWORK_ERROR_THREAD_FAILED;
        }
        free(*server);
        *server = NULL;
        return NETWORK_ERROR_THREAD_FAILED;
    }

    return ERROR_NONE;
}

static int close_server_socket(int ssd);

bool destroy_network_server(network_server_t *server) {
    int res;

    server->shutdown = true;
    shutdown(server->ssd, SHUT_RD);
    close_server_socket(server->ssd); // this should unblock the thread
    if (thrd_join(server->server_thread, &res) != thrd_success) {
        printf("failed to join server thread\n"); // TODO: log with high severity
        return false; // we cannot continue destruction in this case
    }

    // no new connections can be created at this point

    // Maintenance thread will recognize server shutdown and close all remaining connections.
    // All connections that could be closed will have been closed when rejoining that thread.

    if (thrd_join(server->maintenance_thread, &res) != thrd_success) {
        printf("failed to join server thread\n"); // TODO: log with high severity
        return false; // we cannot continue destruction in this case
    }

    if (res != ERROR_NONE) {
        printf("maintenance thread was unable to close all connections\n"); // TODO: log with high severity
        return false; // we cannot continue destruction in this case
    }

    free(server);

    return true;
}

error_t send_to_network(network_connection_t *connection, char *content, int length) {
    if (connection->shutdown || !connection->has_send_mutex) {
        return NETWORK_ERROR_SHUTDOWN;
    }

    if (mtx_lock(&connection->send_mutex) != thrd_success) {
        return ERROR_UNSPECIFIC;
    }

    if (length == NETWORK_SEND_COMPLETE_STRING) {
        length = strlen(content);
    }

    // check that the new content actually fits into the available capacity
    // without a buffer overrun
    // same position indicates "all has been read", so we the maximum possible capacity
    // is buffer size - 1 to keep the write pointer at least one position away from the
    // read pointer
    int capacity_to_read_pos = 0;
    if (connection->send_read_pos > connection->send_write_pos) {
        // read pointer is in front of the write pointer
        capacity_to_read_pos = connection->send_read_pos - connection->send_write_pos - 1;
    } else if (connection->send_read_pos == connection->send_write_pos) {
        // read pointer is at our current position, i.e. everything has been read already
        capacity_to_read_pos = SEND_BUFFER_SIZE - 1;
    } else {
        // read pointer is behind the write pointer
        capacity_to_read_pos = SEND_BUFFER_SIZE - connection->send_write_pos + connection->send_read_pos - 1;
    }

    if (capacity_to_read_pos < length) {
        mtx_unlock(&connection->send_mutex);
        return NETWORK_ERROR_CAPACITY;
    }

    // first copy: until end of buffer is reached
    int buffer_remaining = SEND_BUFFER_SIZE - connection->send_write_pos;
    int num_copy_to_end = _min(buffer_remaining, length);

    memcpy(connection->send_ringbuffer + connection->send_write_pos, content, num_copy_to_end);
    connection->send_write_pos += num_copy_to_end;

    // wrap back to start if end of buffer has been reached
    if (connection->send_write_pos >= SEND_BUFFER_SIZE) {
        connection->send_write_pos = 0;
    }

    // second copy: continue at start of buffer if wrapped
    int num_copy_at_start = length - num_copy_to_end;
    if (num_copy_at_start > 0) {
        memcpy(connection->send_ringbuffer, content + num_copy_to_end, num_copy_at_start);
        connection->send_write_pos += num_copy_at_start;
    }

    cnd_broadcast(&connection->send_wait);
    mtx_unlock(&connection->send_mutex);

    return ERROR_NONE;
}

void close_network_connection(network_connection_t *connection) {
    connection->closing = true;
}

#ifdef TARGET_LINUX
#include "network_linux.c"
#elif TARGET_WINDOWS
#include "network_windows.c"
#else
#error "OS-specific implementation for network.h is missing; target OS is not supported"
#endif
