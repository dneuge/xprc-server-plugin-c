#include <stdlib.h>
#include <string.h>

#include "session.h"
#include "utils.h"

error_t create_session(session_t **session, network_connection_t *connection, server_t *server) {
    *session = malloc(sizeof(session_t));
    if (!(*session)) {
        return ERROR_MEMORY_ALLOCATION;
    }

    memset(*session, 0, sizeof(session_t));

    (*session)->connection = connection;
    (*session)->server = server;
    
    (*session)->channels = create_channels_table();
    if (!(*session)->channels) {
        free(*session);
        *session = NULL;
        return ERROR_MEMORY_ALLOCATION;
    }

    return ERROR_NONE;
}

void destroy_session(session_t *session) {
    // FIXME: terminate all open commands/tasks
    destroy_channels_table(session->channels);
    free(session);
}

int64_t millis_since_reference(session_t *session) {
    if (session->reference_millis < 0) {
        return -1;
    }

    struct timespec now = {0};
    if (!timespec_get(&now, TIME_UTC)) {
        return -1;
    }
    
    int64_t now_millis = millis_of_timespec(&now);
    if (now_millis < 0) {
        return -1;
    }

    if (now_millis < session->reference_millis) {
        return -1;
    }

    return now_millis - session->reference_millis;
}
