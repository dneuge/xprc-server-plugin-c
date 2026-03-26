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
#include <stdlib.h>
#include <string.h>

#include "threads_compat.h"
#include "types_compat.h"

#include "logger.h"
#include "network.h"

#include "settings.h"
#include "utils.h"

#if defined(TARGET_LINUX) || defined(TARGET_MACOS)
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

// suppress SIGPIPE on Linux which has MSG_NOSIGNAL
// macOS has a socket option instead, Windows doesn't know the signal at all
#ifdef TARGET_LINUX
#define NETWORK_SEND_FLAGS (MSG_NOSIGNAL)
#else
#define NETWORK_SEND_FLAGS (0)
#endif

/**
 * Closes the given (server) socket descriptor using the correct function depending on operating system.
 * @param sd (server) socket descriptor to close
 * @return result forwarded from OS function
 */
static int close_socket(int sd);

/**
 * Queries the remote port number for a given socket descriptor.
 * @param sd socket descriptor
 * @return remote port number or 0 on error/if unknown
 */
static uint16_t get_remote_port(int sd);

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
    uint16_t remote_port;
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

    set_current_thread_name("XPRC send %u", connection->remote_port);

    if (mtx_lock(&connection->send_mutex) != thrd_success) {
        RCLOG_WARN("failed to get initial lock of mutex in send thread");
    } else {
        holds_lock = true;
    }

    while (holds_lock && !connection->shutdown) {
        if (connection->send_read_pos == connection->send_write_pos) {
            // everything has been sent (pointers are equal), go to sleep
            if (time_from_now(&wait_until, SEND_CHECK_INTERVAL_MICROSECONDS) != ERROR_NONE) {
                RCLOG_WARN("unable to calculate time to wait in send thread");
                break;
            }

            int res = cnd_timedwait(&connection->send_wait, &connection->send_mutex, &wait_until);
            if (res != thrd_success && res != thrd_timedout) {
                RCLOG_WARN("condition failed to wait on send thread");
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

        ssize_t sent = send(connection->sd, connection->send_ringbuffer + connection->send_read_pos, chunk_size, NETWORK_SEND_FLAGS);
        if (sent <= 0) {
            RCLOG_WARN("failed to send");
            break;
        }

        // we need to regain the lock to continue
        if (mtx_lock(&connection->send_mutex) != thrd_success) {
            RCLOG_WARN("failed to regain lock on mutex in send thread");
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

    RCLOG_DEBUG("send thread terminates connection");
    close_network_connection(connection);

    return 0;
}

static int run_receive_thread(void *arg) {
    network_connection_t *connection = arg;

    set_current_thread_name("XPRC recv %u", connection->remote_port);

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
            // warn only if not intentional
            if (!(connection->shutdown || connection->closing)) {
                RCLOG_WARN("error while reading from connection");
            }
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
                    RCLOG_WARN("receive buffer exceeded");
                    abort = true;
                    break;
                }
            }
        }

        if (abort) {
            break;
        }
    }

    RCLOG_DEBUG("receive thread terminates connection");
    close_network_connection(connection);

    return 0;
}

static bool shutdown_network_connection(network_connection_t *connection) {
    int res;
    bool success = true;

    if (!connection->in_use) {
        RCLOG_DEBUG("connection is not in use");
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
        close_network_connection(connection);
        connection->socket_closed = true;
    }

    if (!connection->joined_recv_thread) {
        if (thrd_join(connection->recv_thread, &res) == thrd_success) {
            connection->joined_recv_thread = true;
        } else {
            RCLOG_ERROR("failed to join client receive thread");
            success = false;
        }
    }

    if (!connection->joined_send_thread) {
        if (thrd_join(connection->send_thread, &res) == thrd_success) {
            connection->joined_send_thread = true;
        } else {
            RCLOG_ERROR("failed to join client send thread");
            success = false;
        }
    }

    if (!success) {
        RCLOG_ERROR("connection could not be terminated");
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

    set_current_thread_name("XPRC maintain");

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

    set_current_thread_name("XPRC server");

    int res = 0;
    unsigned int consecutive_errors = 0;

    while (!server->shutdown && (consecutive_errors < SERVER_MAX_CONSECUTIVE_ERRORS)) {
        int sd = accept(server->ssd, NULL, NULL);
        if (sd < 0) {
            // do not warn/count as an error if socket was intended to be closed
            if (!server->shutdown) {
                RCLOG_WARN("accept failed");
                consecutive_errors++;
            }
            continue;
        }

#ifdef TARGET_MACOS
        // disable SIGPIPE when sending to a closed socket
        // not available on Linux (we need to use MSG_NOSIGNAL there) and irrelevant on Windows (no such signal)
        res = setsockopt(sd, SOL_SOCKET, SO_NOSIGPIPE, &SOCKOPT_ENABLE_VALUE, SOCKOPT_ENABLE_SIZE);
        if (res) {
            RCLOG_WARN("failed to enable NOSIGPIPE on socket: %d, errno %d", res, errno);
        }
#endif

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
            RCLOG_WARN("maximum number of connections reached");
            close_socket(sd);
            continue;
        }

        network_connection_t *connection = &(server->connections[connection_id]);
        memset(connection, 0, sizeof(network_connection_t));
        connection->in_use = true;
        connection->sd = sd;
        connection->server = server;
        connection->remote_port = get_remote_port(sd);

        if (mtx_init(&connection->send_mutex, mtx_plain) != thrd_success) {
            RCLOG_ERROR("failed to initialize send mutex");
            consecutive_errors++;
            close_network_connection(connection);
            continue;
        }
        connection->has_send_mutex = true;

        if (cnd_init(&connection->send_wait) != thrd_success) {
            RCLOG_ERROR("failed to initialize send condition");
            consecutive_errors++;
            close_network_connection(connection);
            continue;
        }
        connection->has_send_wait = true;

        connection->send_ringbuffer = zmalloc(SEND_BUFFER_SIZE);
        if (!connection->send_ringbuffer) {
            RCLOG_ERROR("failed to allocate send ring buffer");
            consecutive_errors++;
            close_network_connection(connection);
            continue;
        }

        if (thrd_create(&connection->send_thread, run_send_thread, connection) != thrd_success) {
            RCLOG_ERROR("failed to spawn send thread");
            consecutive_errors++;
            close_network_connection(connection);
            continue;
        }

        if (thrd_create(&connection->recv_thread, run_receive_thread, connection) != thrd_success) {
            RCLOG_ERROR("failed to spawn receive thread");
            consecutive_errors++;
            close_network_connection(connection);
            continue;
        }

        res = server->handler.new_connection(connection, &connection->handler_reference, server->handler.new_connection_constructor_reference);
        if (res != ERROR_NONE) {
            RCLOG_WARN("handler failed to create connection: %d", res);
            consecutive_errors++;
            close_network_connection(connection);
            continue;
        }

        consecutive_errors = 0;
    }

    RCLOG_DEBUG("server thread shutting down");
    close_socket(server->ssd);

    server->server_thread_stopped = true;

    return 0;
}

static struct sockaddr* create_address_ipv4(network_server_config_t *config) {
    struct sockaddr_in *address = zmalloc(sizeof(struct sockaddr_in));
    if (!address) {
        return NULL;
    }

    address->sin_family = AF_INET;
    address->sin_port = htons(config->port);

    if (!config->interface_address) {
        // FIXME: re-test on Windows (Linux needs htonl)
        address->sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (!strcmp(config->interface_address, INTERFACE_LOCAL)) {
        // FIXME: re-test on Windows (Linux needs htonl)
        address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    } else if (!is_ipv4_address(config->interface_address)) {
        RCLOG_WARN("Not a valid IPv4 address for interface: \"%s\"", config->interface_address);
        free(address);
        return NULL;
    } else {
        // FIXME: this is for Linux; seems to be the same on Windows but needs to be tested
        // Microsoft API docs:
        // [sdk-api] docs/sdk-api-src/content/ws2tcpip/nf-ws2tcpip-inet_pton.md
        int res = inet_pton(AF_INET, config->interface_address, &address->sin_addr.s_addr);
        if (res != 1) {
            RCLOG_WARN("unable to resolve interface \"%s\" (system error %d)", config->interface_address, res);
            free(address);
            return NULL;
        }
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

    if (!server || !config) {
        RCLOG_WARN("create_network_server missing parameters: server=%p, config=%p", server, config);
        return ERROR_UNSPECIFIC;
    }

    struct sockaddr *address = create_address(config);
    socklen_t address_size = config->enable_ipv6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
    if (!address) {
        RCLOG_WARN("failed to create server address");
        return NETWORK_ERROR_BAD_ADDRESS;
    }

    *server = zmalloc(sizeof(network_server_t));
    if (!*server) {
        free(address);
        return ERROR_MEMORY_ALLOCATION;
    }

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
        RCLOG_WARN("failed to enable REUSEADDR on socket: %d, errno %d", res, errno);
    }

    res = setsockopt((*server)->ssd, SOL_SOCKET, SO_KEEPALIVE, &SOCKOPT_ENABLE_VALUE, SOCKOPT_ENABLE_SIZE);
    if (res) {
        RCLOG_WARN("failed to enable KEEPALIVE on socket: %d, errno %d", res, errno);
    }

    res = bind((*server)->ssd, (struct sockaddr*) address, address_size);
    free(address);
    if (res) {
        RCLOG_WARN("failed to bind on requested address: %d, errno %d", res, errno);
        close_socket((*server)->ssd);
        free(*server);
        *server = NULL;
        return NETWORK_ERROR_BIND_FAILED;
    }

    if (listen((*server)->ssd, CONNECTION_BACKLOG)) {
        RCLOG_WARN("failed to listen on server socket");
        close_socket((*server)->ssd);
        free(*server);
        *server = NULL;
        return NETWORK_ERROR_LISTEN_FAILED;
    }

    if (thrd_create(&(*server)->server_thread, run_server_thread, *server) != thrd_success) {
        RCLOG_ERROR("failed to spawn server thread");
        close_socket((*server)->ssd);
        free(*server);
        *server = NULL;
        return NETWORK_ERROR_THREAD_FAILED;
    }

    if (thrd_create(&(*server)->maintenance_thread, run_maintenance_thread, *server) != thrd_success) {
        RCLOG_ERROR("failed to spawn maintenance thread");
        (*server)->shutdown = true;
        shutdown((*server)->ssd, SHUT_RD);
        close_socket((*server)->ssd);
        if (thrd_join((*server)->server_thread, &res) != thrd_success) {
            RCLOG_ERROR("failed to rejoin server thread during failed construction cleanup");
            return NETWORK_ERROR_THREAD_FAILED;
        }
        free(*server);
        *server = NULL;
        return NETWORK_ERROR_THREAD_FAILED;
    }

    return ERROR_NONE;
}

bool destroy_network_server(network_server_t *server) {
    int res;

    if (!server) {
        return true;
    }

    server->shutdown = true;
    shutdown(server->ssd, SHUT_RD);
    close_socket(server->ssd); // this should unblock the thread
    if (thrd_join(server->server_thread, &res) != thrd_success) {
        RCLOG_ERROR("failed to join server thread");
        return false; // we cannot continue destruction in this case
    }

    // no new connections can be created at this point

    // Maintenance thread will recognize server shutdown and close all remaining connections.
    // All connections that could be closed will have been closed when rejoining that thread.

    if (thrd_join(server->maintenance_thread, &res) != thrd_success) {
        RCLOG_ERROR("failed to join server thread");
        return false; // we cannot continue destruction in this case
    }

    if (res != ERROR_NONE) {
        RCLOG_ERROR("maintenance thread was unable to close all connections");
        return false; // we cannot continue destruction in this case
    }

    free(server);

    return true;
}

error_t send_to_network(network_connection_t *connection, char *content, int length) {
    if (!connection || !content) {
        RCLOG_WARN("send_to_network missing parameters: connection=%p, content=%p", connection, content);
        return ERROR_UNSPECIFIC;
    }

    if (connection->shutdown || !connection->has_send_mutex) {
        return NETWORK_ERROR_SHUTDOWN;
    }

    if (mtx_lock(&connection->send_mutex) != thrd_success) {
        return ERROR_UNSPECIFIC;
    }

    if (length == NETWORK_SEND_COMPLETE_STRING) {
        length = strlen(content);
    }

    if (length < 0) {
        RCLOG_WARN("send_to_network bad length=%d", length);
        return ERROR_UNSPECIFIC;
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
    if (!connection) {
        return;
    }

    // FIXME: set closing atomic
    if (connection->closing) {
        return;
    }
    connection->closing = true;

    close_socket(connection->sd);
}

error_t copy_network_config_to(network_server_config_t *source, network_server_config_t *destination) {
    if (!source || !destination) {
        RCLOG_WARN("copy_network_config_to missing parameters: source=%p, destination=%p", source, destination);
        return ERROR_UNSPECIFIC;
    }

    memcpy(destination, source, sizeof(network_server_config_t));

    if (destination->interface_address) {
        destination->interface_address = copy_string(destination->interface_address);
        if (!destination->interface_address) {
            RCLOG_WARN("[network] failed to copy interface address between configurations");
            return ERROR_MEMORY_ALLOCATION;
        }
    }

    return ERROR_NONE;
}

void destroy_network_config_contents(network_server_config_t *config) {
    if (!config) {
        return;
    }

    if (config->interface_address) {
        free(config->interface_address);
    }

    memset(config, 0, sizeof(network_server_config_t));
}


#if defined(TARGET_LINUX) || defined(TARGET_MACOS)
// FIXME: verify this actually works on MacOS an does not just compile
#include "network_linux.c"
#elif TARGET_WINDOWS
#include "network_windows.c"
#else
#error "OS-specific implementation for network.h is missing; target OS is not supported"
#endif
