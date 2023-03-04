#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "channels.h"
#include "session.h"
#include "utils.h"

#define CHANNEL_ACTION_ERR 0
#define CHANNEL_ACTION_ACK_CONTINUE 1
#define CHANNEL_ACTION_ACK_CLOSE 2
#define CHANNEL_ACTION_CONTINUE 3
#define CHANNEL_ACTION_CLOSE 4

typedef uint8_t channel_action_t;

error_t create_session(session_t **session, network_connection_t *connection, server_t *server) {
    *session = malloc(sizeof(session_t));
    if (!(*session)) {
        return ERROR_MEMORY_ALLOCATION;
    }

    memset(*session, 0, sizeof(session_t));

    if (mtx_init(&(*session)->mutex, mtx_plain | mtx_recursive) != thrd_success) {
        return ERROR_UNSPECIFIC;
    }

    (*session)->connection = connection;
    (*session)->server = server;
    
    (*session)->channels = create_channels_table();
    if (!(*session)->channels) {
        mtx_destroy(&(*session)->mutex);
        free(*session);
        *session = NULL;
        return ERROR_MEMORY_ALLOCATION;
    }

    return ERROR_NONE;
}

int64_t millis_since_reference(session_t *session) {
    // called during flight loop; keep lock-free
    
    if (session->destruction_pending || session->reference_millis < 0) {
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

static error_t send_channel(session_t *session, channel_t *channel, channel_action_t action, int64_t timestamp, char *message) {
    error_t err = ERROR_NONE;
    
    if (timestamp == CURRENT_TIME_REFERENCE) {
        timestamp = millis_since_reference(session);
    }

    if (timestamp < 0) {
        return ERROR_UNSPECIFIC;
    }

    if (channel->state == CHANNEL_STATE_CLOSED) {
        // never send anything in reference to an already closed channel
        return SESSION_ERROR_INVALID_CHANNEL_STATE;
    }

    if ((channel->state == CHANNEL_STATE_CONFIRMED) && (action == CHANNEL_ACTION_ACK_CONTINUE || action == CHANNEL_ACTION_ACK_CLOSE)) {
        // duplicate ACK attempted
        return SESSION_ERROR_INVALID_CHANNEL_STATE;
    }

    if ((channel->state == CHANNEL_STATE_INITIAL) && (action == CHANNEL_ACTION_CONTINUE)) {
        // ACK or ERR required for channel initialization
        return SESSION_ERROR_INVALID_CHANNEL_STATE;
    }

    bool channel_continues = (action == CHANNEL_ACTION_CONTINUE) || (action == CHANNEL_ACTION_ACK_CONTINUE);
    char continuation_indicator = channel_continues ? '+' : '-';
    
    char channel_name[5] = {0};
    channel_id_to_string(channel->id, channel_name);

    int timestamp_length = num_digits(timestamp);
    if (timestamp_length < 0) {
        timestamp = 0;
        timestamp_length = 1;
    }

    int message_length = (message != NULL) ? strlen(message) : 0;
    bool needs_state_prefix = (message_length == 0) || (channel->state == CHANNEL_STATE_INITIAL);
    
    int out_length = 1 /* continuation */ + 4 /* channel ID */ + 1 /* space */ + timestamp_length + 1 /* LF */;
    if (message_length > 0) {
        out_length += 1 /* space */ + message_length;
    }
    if (needs_state_prefix) {
        out_length += 4 /* ACK/ERR + space */;
    }
    
    char *out = zalloc(out_length + 1);
    if (!out) {
        // TODO: close channel?
        return ERROR_MEMORY_ALLOCATION;
    }

    out[0] = continuation_indicator;
    char *dest = out + 1;

    if (needs_state_prefix) {
        char *state_prefix = (action == CHANNEL_ACTION_ERR) ? "ERR " : "ACK ";
        memcpy(dest, state_prefix, 4);
        dest += 4;
    }

    memcpy(dest, channel_name, 4);
    dest += 4;
    
    *(dest++) = ' ';

    snprintf(dest, timestamp_length + 1, "%ld", timestamp);
    dest += timestamp_length;

    if (message_length > 0) {
        *(dest++) = ' ';
        memcpy(dest, message, message_length);
        dest += message_length;
    }

    *(dest++) = '\n';

    err = send_to_network(session->connection, out, out_length);
    
    free(out);

    if (channel_continues) {
        channel->state = CHANNEL_STATE_CONFIRMED;
    } else {
        channel->state = CHANNEL_STATE_CLOSED;
    }

    return err;
}

error_t confirm_channel(session_t *session, channel_id_t channel_id, int64_t timestamp, char *message) {
    printf("[XPRC] confirm_channel: %s\n", message); // DEBUG
    
    if (!lock_session(session)) {
        return ERROR_LOCK_FAILED;
    }
    
    channel_t *channel = get_channel(session->channels, channel_id);
    if (!channel) {
        return SESSION_ERROR_NO_SUCH_CHANNEL;
    }
    
    error_t err = send_channel(session, channel, CHANNEL_ACTION_ACK_CONTINUE, timestamp, message);
    if (err != ERROR_NONE) {
        error_channel(session, channel_id, timestamp, "failed to confirm channel");
    }
    
    unlock_session(session);
    
    printf("[XPRC] confirm_channel done with %d\n", err); // DEBUG
    return err;
}

error_t continue_channel(session_t *session, channel_id_t channel_id, int64_t timestamp, char *message) {
    printf("[XPRC] continue_channel: %s\n", message); // DEBUG
    
    if (!lock_session(session)) {
        return ERROR_LOCK_FAILED;
    }
    
    channel_t *channel = get_channel(session->channels, channel_id);
    if (!channel) {
        return SESSION_ERROR_NO_SUCH_CHANNEL;
    }
    
    error_t err = send_channel(session, channel, CHANNEL_ACTION_CONTINUE, timestamp, message);
    if (err != ERROR_NONE) {
        error_channel(session, channel_id, timestamp, "failed to continue channel");
    }
    
    unlock_session(session);
    
    printf("[XPRC] continue_channel done with %d\n", err); // DEBUG
    return err;
}

error_t finish_channel(session_t *session, channel_id_t channel_id, int64_t timestamp, char *message) {
    printf("[XPRC] finish_channel: %s\n", message); // DEBUG

    if (!lock_session(session)) {
        return ERROR_LOCK_FAILED;
    }
    
    channel_t *channel = get_channel(session->channels, channel_id);
    if (!channel) {
        return SESSION_ERROR_NO_SUCH_CHANNEL;
    }

    channel_action_t action = (channel->state == CHANNEL_STATE_INITIAL) ? CHANNEL_ACTION_ACK_CLOSE : CHANNEL_ACTION_CLOSE;
    
    error_t err = send_channel(session, channel, action, timestamp, message);
    if (err != ERROR_NONE) {
        error_channel(session, channel_id, timestamp, "failed to finish channel");
    }

    unlock_session(session);

    printf("[XPRC] finish_channel done with %d\n", err); // DEBUG
    return err;
}

error_t error_channel(session_t *session, channel_id_t channel_id, int64_t timestamp, char *message) {
    printf("[XPRC] error_channel: %s\n", message); // DEBUG

    if (!lock_session(session)) {
        return ERROR_LOCK_FAILED;
    }
    
    channel_t *channel = get_channel(session->channels, channel_id);
    if (!channel) {
        return SESSION_ERROR_NO_SUCH_CHANNEL;
    }

    error_t err = send_channel(session, channel, CHANNEL_ACTION_ERR, timestamp, message);

    unlock_session(session);

    printf("[XPRC] error_channel done with %d\n", err); // DEBUG
    return err;
}

static void destroy_session_channel(channel_t *channel, void *ref) {
    printf("[XPRC] destroy_session_channel\n"); // DEBUG
    
    session_t *session = ref;

    if (!channel->destruction_requested) {
        if (channel->command && channel->command->terminate) {
            // TODO: log error
            channel->command->terminate(channel->command_ref);
        }

        if (channel->state != CHANNEL_STATE_CLOSED) {
            send_channel(session, channel, CHANNEL_ACTION_CLOSE, CURRENT_TIME_REFERENCE, NULL);
        }
    }

    if (channel->command && channel->command->destroy) {
        // TODO: log error
        channel->command->destroy(channel->command_ref);
    }

    printf("[XPRC] destroy_session_channel: freeing channel\n"); // DEBUG
    free(channel);
    printf("[XPRC] destroy_session_channel: done\n"); // DEBUG
}

void destroy_session(session_t *session) {
    session->destruction_pending = true;
    lock_session(session); // will fail; needed to make sure all threads noticed pending destruction
    
    destroy_channels_table(session->channels, destroy_session_channel, session);
    mtx_destroy(&session->mutex);
    free(session);
}

bool lock_session(session_t *session) {
    if (session->destruction_pending) {
        return false;
    }
    
    if (mtx_lock(&session->mutex) != thrd_success) {
        return false;
    }

    if (session->destruction_pending) {
        mtx_unlock(&session->mutex);
        return false;
    }

    return true;
}

void unlock_session(session_t *session) {
    mtx_unlock(&session->mutex);
}
