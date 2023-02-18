#ifndef SESSION_H
#define SESSION_H

#include <stdint.h>
#include <time.h>

#include "channels.h"
#include "network.h"

// break circular dependency in definitions
typedef struct _session_t session_t;

#include "server.h"

#define SESSION_PHASE_AWAIT_VERSION 0
#define SESSION_PHASE_AWAIT_PASSWORD 1
#define SESSION_PHASE_FAILED 2
#define SESSION_PHASE_LOGGED_IN 3

typedef uint8_t session_phase_t;

typedef struct _session_t {
    struct timespec reference_time;
    int64_t reference_millis;
    session_phase_t phase;
    int client_version;
    channels_table_t *channels;
    network_connection_t *connection;
    server_t *server;
} session_t;

error_t create_session(session_t **session, network_connection_t *connection, server_t *server);
void destroy_session(session_t *session);

int64_t millis_since_reference(session_t *session);

#endif
