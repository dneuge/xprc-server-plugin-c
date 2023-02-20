#ifndef SESSION_H
#define SESSION_H

#include <stdint.h>
#include <time.h>

typedef struct _session_t session_t;

#include "channels.h"
#include "network.h"
#include "server.h"

#define SESSION_ERROR_NO_SUCH_CHANNEL       (SESSION_ERROR_BASE + 0)
#define SESSION_ERROR_INVALID_CHANNEL_STATE (SESSION_ERROR_BASE + 1)

#define SESSION_PHASE_AWAIT_VERSION 0
#define SESSION_PHASE_AWAIT_PASSWORD 1
#define SESSION_PHASE_FAILED 2
#define SESSION_PHASE_LOGGED_IN 3

#define CURRENT_TIME_REFERENCE -1

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

error_t confirm_channel(session_t *session, channel_id_t channel_id, int64_t millis_since_reference, char *message);
error_t continue_channel(session_t *session, channel_id_t channel_id, int64_t millis_since_reference, char *message);
error_t finish_channel(session_t *session, channel_id_t channel_id, int64_t millis_since_reference, char *message);
error_t error_channel(session_t *session, channel_id_t channel_id, int64_t millis_since_reference, char *message);

#endif
