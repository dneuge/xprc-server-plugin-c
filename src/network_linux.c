#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <unistd.h>

#include "utils.h"

#include "network.h"

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

static inline int min(int a, int b) {
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

        int chunk_size = min(pending, SEND_CHUNK_SIZE);
            
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

    if (!config->interface) {
        address->sin_addr.s_addr = INADDR_ANY;
    } else if (!strcmp(config->interface, INTERFACE_LOCAL)) {
        address->sin_addr.s_addr = INADDR_LOOPBACK;
    } else {
        // TODO: support selection of specific interface to bind to
        printf("unable to resolve interface \"%s\"\n", config->interface); // TODO: log
        free(address);
        return NULL;
    }
    
    return (struct sockaddr*) address;
}

static struct sockaddr* create_address_ipv6(network_server_config_t *config) {
    struct sockaddr_in6 *address = malloc(sizeof(struct sockaddr_in6));
    if (!address) {
        return NULL;
    }
    
    memset(address, 0, sizeof(struct sockaddr_in6));
    
    address->sin6_family = AF_INET6;
    address->sin6_port = htons(config->port);

    if (!config->interface) {
        address->sin6_addr = in6addr_any;
    } else if (!strcmp(config->interface, INTERFACE_LOCAL)) {
        address->sin6_addr = in6addr_loopback;
    } else {
        // TODO: support selection of specific interface to bind to
        printf("unable to resolve interface \"%s\"\n", config->interface); // TODO: log
        free(address);
        return NULL;
    }
    
    return (struct sockaddr*) address;
}

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

    res = setsockopt((*server)->ssd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
    if (res) {
        printf("failed to enable REUSEADDR on socket: %d, errno %d\n", res, errno); // TODO: log
    }
    
    res = setsockopt((*server)->ssd, SOL_SOCKET, SO_KEEPALIVE, &(int){1}, sizeof(int));
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

bool destroy_network_server(network_server_t *server) {
    int res;
    
    server->shutdown = true;
    shutdown(server->ssd, SHUT_RD);
    close(server->ssd); // this should unblock the thread
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
    int num_copy_to_end = min(buffer_remaining, length);
    
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
            name = copy_string(ntop_buffer);
        }

        if (name) {
            if (!list_append(out, name)) {
                free(name);
                goto error;
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