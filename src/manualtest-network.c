#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "threads_compat.h"

#include "logger.h"
#include "network.h"
#include "utils.h"

#define MAX_LINES_PER_CONNECTION 5
#define PORT 23042

int global_id;

typedef struct {
    int id;
    int remaining;
    network_connection_t *connection;
} myref_t;

static int new_connection(network_connection_t *connection, void **handler_reference, void *constructor_reference) {
    myref_t *myref = zmalloc(sizeof(myref_t));
    if (!myref) {
        RCLOG_WARN("failed to allocate handler reference");
        return ERROR_UNSPECIFIC;
    }
    *handler_reference = myref;

    myref->id = (global_id++);
    myref->remaining = MAX_LINES_PER_CONNECTION;
    myref->connection = connection;
    
    char msg[2048] = {0};
    snprintf(msg, 2047, "connected as #%d\n", myref->id);
    int res = send_to_network(myref->connection, msg, strlen(msg));
    if (res != ERROR_NONE) {
        RCLOG_WARN("send failed with %d", res);
    }
    
    return ERROR_NONE;
}

static void on_line_received(void *handler_reference, char *line, int length) {
    myref_t *myref = handler_reference;
    
    myref->remaining--;
    
    char msg[2048] = {0};
    int res;
    
    char *copy = copy_partial_string(line, length);
    if (!copy) {
        RCLOG_WARN("#%d received something but copy failed", myref->id);
        snprintf(msg, 2047, "you are #%d, %d lines remaining. no echo\n", myref->id, myref->remaining);
        res = send_to_network(myref->connection, msg, strlen(msg));
    } else {
        RCLOG_INFO("#%d received: \"%s\"", myref->id, copy);
        snprintf(msg, 2047, "you are #%d, %d lines remaining. echo: %s\n", myref->id, myref->remaining, copy);
        res = send_to_network(myref->connection, msg, strlen(msg));
        free(copy);
    }
    
    if (res != ERROR_NONE) {
        RCLOG_WARN("#%d failed to send: %d", myref->id, res);
    }

    if (myref->remaining <= 0) {
        RCLOG_WARN("#%d received maximum number of lines, closing", myref->id);
        close_network_connection(myref->connection);
    }
}

static void on_connection_closing(void *handler_reference) {
    myref_t *myref = handler_reference;

    RCLOG_INFO("#%d is closing", myref->id);
    free(handler_reference);
}

static network_server_t *server;
static bool shutdown = false;

void signal_shutdown() {
    shutdown = true;
}

int main(int argc, char **argv) {
    int res;

    xprc_log_init();
    xprc_set_min_log_level_console(RCLOG_LEVEL_TRACE);

    error_t err = initialize_os_network_apis();
    if (err != ERROR_NONE) {
        RCLOG_ERROR("failed to initialize OS network APIs: %d", err);
        return 1;
    }

    network_server_config_t config = {
        .enable_ipv6 = true,
        .interface_address = NULL,
        .port = PORT,
    };

    network_handler_t handler = {
        .new_connection = new_connection,
        .on_line_received = on_line_received,
        .on_connection_closing = on_connection_closing,
    };

    server = NULL;
    res = create_network_server(&server, &config, handler);
    if (res != ERROR_NONE) {
        RCLOG_ERROR("server could not be created: %d", res);
        return 1;
    }
    RCLOG_INFO("waiting for connections on port %d", PORT);

    signal(SIGTERM, signal_shutdown);
    signal(SIGINT, signal_shutdown);

#ifndef TARGET_WINDOWS
    signal(SIGHUP, signal_shutdown);
#endif

    while (!shutdown) {
        thrd_sleep(&(struct timespec){.tv_sec=1,.tv_nsec=0}, NULL);
    }

    RCLOG_INFO("program terminates");
    if (server) {
        RCLOG_DEBUG("destroying server");
        destroy_network_server(server); // TODO: use return value
    }
    RCLOG_INFO("complete");

    xprc_log_destroy();

    return 0;
}
