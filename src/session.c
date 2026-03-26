#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "channels.h"
#include "session.h"

#include "logger.h"
#include "utils.h"

#define CHANNEL_ACTION_ERR 0
#define CHANNEL_ACTION_ACK_CONTINUE 1
#define CHANNEL_ACTION_ACK_CLOSE 2
#define CHANNEL_ACTION_CONTINUE 3
#define CHANNEL_ACTION_CLOSE 4

#ifdef TARGET_MACOS
// FIXME: detect in CMake and provide a define specific for size/type of timestamps instead
#define TIMESTAMP_FORMAT "%lld"
#else
#define TIMESTAMP_FORMAT "%ld"
#endif

typedef uint8_t channel_action_t;

error_t create_session(session_t **session, network_connection_t *connection, server_t *server) {
    error_t out_error = ERROR_NONE;
    list_t *command_names = NULL;

    if (!session || !connection || !server) {
        RCLOG_ERROR("create_session missing parameters: session=%p, connection=%p, server=%p", session, connection, server);
        return ERROR_UNSPECIFIC;
    }

    command_factory_t *command_factory = server->config.command_factory;
    if (!command_factory) {
        RCLOG_ERROR("create_session called without command factory");
        return ERROR_UNSPECIFIC;
    }

    *session = zmalloc(sizeof(session_t));
    if (!(*session)) {
        return ERROR_MEMORY_ALLOCATION;
    }

    if (mtx_init(&(*session)->mutex, mtx_plain | mtx_recursive) != thrd_success) {
        return ERROR_UNSPECIFIC;
    }

    (*session)->connection = connection;
    (*session)->server = server;
    
    (*session)->channels = create_channels_table();
    if (!(*session)->channels) {
        RCLOG_WARN("create_session: failed to create channel table");
        out_error = ERROR_MEMORY_ALLOCATION;
        goto error;
    }

    (*session)->command_configs = create_hashmap();
    if (!(*session)->command_configs) {
        RCLOG_WARN("create_session: failed to create command config map");
        out_error = ERROR_MEMORY_ALLOCATION;
        goto error;
    }

    command_names = list_command_names(command_factory);
    if (!command_names) {
        RCLOG_WARN("create_session: failed to list command names");
        goto error;
    }
    for (list_item_t *command_name_item = command_names->head; command_name_item; command_name_item = command_name_item->next) {
        char *command_name = command_name_item->value;
        command_config_t *default_config = create_default_command_config(command_factory, command_name);
        if (!default_config) {
            RCLOG_WARN("create_session: failed to create default configuration for %s", command_name);
            goto error;
        }

        command_config_t *replaced_config = NULL;
        if (!hashmap_put((*session)->command_configs, command_name, default_config, (void**)&replaced_config)) {
            RCLOG_WARN("create_session: failed to record default configuration for %s", command_name);

            destroy_command_config(default_config);
            default_config = NULL;

            goto error;
        }

        if (replaced_config) {
            RCLOG_ERROR("create_session: command %s has been initialized multiple times, aborting", command_name);

            // replaced config is not tracked and thus must be freed
            destroy_command_config(replaced_config);
            replaced_config = NULL;

            goto error;
        }
    }

    goto end;

error:
    RCLOG_WARN("failed to create session");
    destroy_session(*session);
    *session = NULL;

    if (out_error == ERROR_NONE) {
        out_error = ERROR_UNSPECIFIC;
    }

end:
    if (command_names) {
        destroy_list(command_names, free);
        command_names = NULL;
    }

    return out_error;
}

int64_t millis_since_reference(session_t *session) {
    // called during flight loop; keep lock-free

    if (!session) {
        return -1;
    }

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
        RCLOG_DEBUG("send_channel: invalid timestamp %d", timestamp);
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
    bool needs_state_prefix = (message_length == 0) || (channel->state == CHANNEL_STATE_INITIAL) || (action == CHANNEL_ACTION_ERR);
    
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

    snprintf(dest, timestamp_length + 1, TIMESTAMP_FORMAT, timestamp);
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
    RCLOG_TRACE("confirm_channel: %s", message);
    
    if (!lock_session(session)) {
        return ERROR_MUTEX_FAILED;
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
    
    RCLOG_TRACE("confirm_channel done with %d", err);
    return err;
}

error_t continue_channel(session_t *session, channel_id_t channel_id, int64_t timestamp, char *message) {
    RCLOG_TRACE("continue_channel: %s", message);
    
    if (!lock_session(session)) {
        return ERROR_MUTEX_FAILED;
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
    
    RCLOG_TRACE("continue_channel done with %d", err);
    return err;
}

error_t finish_channel(session_t *session, channel_id_t channel_id, int64_t timestamp, char *message) {
    RCLOG_TRACE("finish_channel: %s", message);

    if (!lock_session(session)) {
        return ERROR_MUTEX_FAILED;
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

    RCLOG_TRACE("finish_channel done with %d", err);
    return err;
}

error_t error_channel(session_t *session, channel_id_t channel_id, int64_t timestamp, char *message) {
    RCLOG_TRACE("error_channel: %s", message);

    if (!lock_session(session)) {
        return ERROR_MUTEX_FAILED;
    }
    
    channel_t *channel = get_channel(session->channels, channel_id);
    if (!channel) {
        return SESSION_ERROR_NO_SUCH_CHANNEL;
    }

    error_t err = send_channel(session, channel, CHANNEL_ACTION_ERR, timestamp, message);

    unlock_session(session);

    RCLOG_TRACE("error_channel done with %d", err);
    return err;
}

static void destroy_session_channel(channel_t *channel, void *ref) {
    RCLOG_TRACE("destroy_session_channel session %p / channel %p", ref, channel);

    error_t err = ERROR_NONE;
    session_t *session = ref;

    if (!channel->destruction_requested) {
        if (channel->command && channel->command->terminate) {
            RCLOG_TRACE("destroy_session_channel: terminating session %p / channel %p / command %p", session, channel, channel->command);
            err = channel->command->terminate(channel->command_ref);
            if (err != ERROR_NONE) {
                RCLOG_WARN("destroy_session_channel: failed to terminate command: %d", err);
            }
        }

        channel->destruction_requested = true;
    }

    if (channel->state != CHANNEL_STATE_CLOSED) {
        if (session->destruction_pending) {
            // If session is being destroyed we don't need to communicate with any potentially still connected client
            // as the connection will be shut down anyway, implicitly terminating all channels at once.
            // send_channel/CURRENT_TIME_REFERENCE will always fail if session is pending destruction, so we can just
            // skip that call altogether.
            RCLOG_DEBUG("destroy_session_channel: channel is not closed yet but session is being destroyed, skip client notification");
        } else {
            RCLOG_DEBUG("destroy_session_channel: channel is not closed yet, closing");
            err = send_channel(session, channel, CHANNEL_ACTION_CLOSE, CURRENT_TIME_REFERENCE, NULL);
            if (err != ERROR_NONE) {
                RCLOG_WARN("destroy_session_channel: failed to close channel: %d", err);
            }
        }

        channel->state = CHANNEL_STATE_CLOSED;
    }

    if (channel->command && channel->command->destroy) {
        err = channel->command->destroy(channel->command_ref);
        if (err != ERROR_NONE) {
            RCLOG_ERROR("destroy_session_channel: failed to destroy command at %p (error %d), refusing to free channel", channel->command_ref, err);
            return;
        }
    }

    RCLOG_TRACE("destroy_session_channel: poisoning channel");
    channel->id = BAD_CHANNEL_ID;
    channel->command = NULL;
    channel->command_ref = NULL;

    RCLOG_TRACE("destroy_session_channel: freeing channel");
    free(channel);
    RCLOG_TRACE("destroy_session_channel: done");
}

static void destroy_command_config_entry(char *key, void *value) {
    destroy_command_config(value);
}

void destroy_session(session_t *session) {
    if (!session) {
        return;
    }

    session->destruction_pending = true;
    lock_session(session); // will fail; needed to make sure all threads noticed pending destruction

    if (session->command_configs) {
        destroy_hashmap(session->command_configs, destroy_command_config_entry);
    }

    destroy_channels_table(session->channels, destroy_session_channel, session);
    mtx_destroy(&session->mutex);
    free(session);
}

bool lock_session(session_t *session) {
    if (!session) {
        RCLOG_WARN("lock_session called with NULL");
        return false;
    }

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
    if (!session) {
        RCLOG_WARN("unlock_session called with NULL");
        return;
    }

    mtx_unlock(&session->mutex);
}

error_t set_command_configuration(session_t *session, char *command_name, command_config_t *config) {
    if (!session) {
        RCLOG_ERROR("set_command_configuration called without session");
        return ERROR_UNSPECIFIC;
    }

    if (!command_name) {
        RCLOG_ERROR("set_command_configuration called without command_name");
        return ERROR_UNSPECIFIC;
    }

    if (!config) {
        RCLOG_ERROR("set_command_configuration called without config");
        return ERROR_UNSPECIFIC;
    }

    if (!lock_session(session)) {
        RCLOG_WARN("set_command_configuration failed to lock session");
        return ERROR_MUTEX_FAILED;
    }

    command_config_t *previous_config = NULL;
    bool success = hashmap_put(session->command_configs, command_name, config, (void**)&previous_config);

    unlock_session(session);

    if (success) {
        RCLOG_DEBUG("stored command config for %s", command_name);
    } else {
        RCLOG_WARN("failed to store command config for %s", command_name);
        previous_config = NULL; // keep existing config, must not be freed
    }

    if (previous_config) {
        RCLOG_DEBUG("freeing old command config for %s at %p", command_name, previous_config)
        destroy_command_config(previous_config);
        previous_config = NULL;
    }

    return success ? ERROR_NONE : ERROR_UNSPECIFIC;
}

command_config_t* get_command_configuration(session_t *session, char *command_name) {
    if (!session) {
        RCLOG_ERROR("get_command_configuration called without session");
        return NULL;
    }

    if (!command_name) {
        RCLOG_ERROR("get_command_configuration called without command_name");
        return NULL;
    }

    if (!lock_session(session)) {
        RCLOG_WARN("get_command_configuration failed to lock session");
        return NULL;
    }

    command_config_t *config = hashmap_get(session->command_configs, command_name);

    unlock_session(session);

    return config;
}
