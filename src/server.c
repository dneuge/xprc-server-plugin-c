#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "password.h"
#include "requests.h"
#include "server.h"

#include "logger.h"
#include "session.h"
#include "utils.h"

#define _STR(x) #x
#define STR(x) _STR(x)

#define SERVER_PROTOCOL_VERSION 1

#define REFERENCE_COARSE_TIMESTAMP_BUFFER_SIZE 30
#define HANDSHAKE_COMPLETION_BUFFER_SIZE 50

__attribute__((__format__ (__printf__, 2, 3)))
static void send_or_close(network_connection_t *connection, char *format, ...) {
    va_list args;
    va_start(args, format);
    char *msg = dynamic_vsprintf(format, args);
    va_end(args);
    
    if (!msg) {
        RCLOG_WARN("terminating connection because formatting channel message failed: %s", format);
        close_network_connection(connection);
        return;
    }
    
    error_t err = send_to_network(connection, msg, NETWORK_SEND_COMPLETE_STRING);
    if (err != ERROR_NONE) {
        RCLOG_WARN("terminating connection because sending channel message failed: %s", msg);
        close_network_connection(connection);
    }

    free(msg);
}

static void handle_command_request(session_t *session, request_t *request) {
    char channel_name[5] = {0};
    channel_id_to_string(request->channel_id, channel_name);

    if (has_channel(session->channels, request->channel_id)) {
        send_or_close(session->connection, "-ERR %s " INT64_FORMAT " channel busy\n", channel_name, millis_since_reference(session));
        return;
    }

    channel_t *channel = zmalloc(sizeof(channel_t));
    if (!channel) {
        send_or_close(session->connection, "-ERR %s " INT64_FORMAT " channel creation failed\n", channel_name, millis_since_reference(session));
        return;
    }

    channel->id = request->channel_id;
    channel->state = CHANNEL_STATE_INITIAL;
    if (!put_channel(session->channels, channel)) {
        send_or_close(session->connection, "-ERR %s " INT64_FORMAT " channel registration failed\n", channel_name, millis_since_reference(session));
        return;
    }
    
    error_t err = create_command(session->server->config.command_factory, channel, session, request);
    if (err != ERROR_NONE) {
        // use session error_channel function instead of raw send_or_close/send_to_network as the channel might have been closed
        // already by the command that just failed to be created
        err = error_channel(session, channel->id, CURRENT_TIME_REFERENCE, "command creation failed");
        if (err != ERROR_NONE && err != SESSION_ERROR_INVALID_CHANNEL_STATE) {
            RCLOG_WARN("terminating connection because sending channel error failed after failed command creation");
            close_network_connection(session->connection);
            return;
        }
        
        if (!pop_channel(session->channels, channel->id)) {
            RCLOG_WARN("failed to clear dead channel %s after command creation failed, closing connection", channel_name);
            close_network_connection(session->connection);
        }
    } else if (!channel->command_ref) {
        RCLOG_DEBUG("command %s completed without command reference on channel %s, freeing channel again (instant command completion)", request->command_name, channel_name);

        // close channel with success indication if still open
        err = finish_channel(session, channel->id, CURRENT_TIME_REFERENCE, NULL);
        if (err != ERROR_NONE && err != SESSION_ERROR_INVALID_CHANNEL_STATE) {
            RCLOG_WARN("terminating connection because finishing channel failed after instant command completion");
            close_network_connection(session->connection);
            return;
        }

        if (!pop_channel(session->channels, channel->id)) {
            RCLOG_WARN("failed to clear instantly completed channel %s, closing connection", channel_name);
            close_network_connection(session->connection);
        }
    }
}

static void handle_termination_request(session_t *session, request_t *request) {
    char channel_name[5] = {0};
    channel_id_to_string(request->channel_id, channel_name);
    
    channel_t *channel = get_channel(session->channels, request->channel_id);
    if (!channel || channel->state == CHANNEL_STATE_CLOSED || channel->destruction_requested) {
        // mandatory error string; do not change (see Special Considerations section in protocol specification)
        // since the channel does not exist/is no longer open we should not send any channel-message but instead need
        // to use an "out-of-band" server message (like a syntax error, except neither server nor client should panic)
        send_or_close(session->connection, "*ERR " INT64_FORMAT " termination request ignored, channel does not exist: %s\n", millis_since_reference(session), channel_name);
        return;
    }

    error_t err = channel->command->terminate(channel->command_ref);
    if (err != ERROR_NONE) {
        RCLOG_WARN("command termination for channel %s failed (error %d), closing connection", channel_name, err);
        close_network_connection(session->connection);
    }
}    

static void handle_request(session_t *session, request_t *request) {
    // Practically all invocations below require locks to both session and task schedule.
    // If we only lock the session here and let the commands lock the schedule, we get a deadlock with flight loop and
    // post-processing calls which lock the schedule before invoked command callbacks lock the session.
    // Locking the schedule here already, before gaining a session lock, ensures that we nest the locks in the
    // same order which solves that deadlock without overhead or more complex workarounds.
    if (lock_schedule(session->server->config.task_schedule) != ERROR_NONE) {
        RCLOG_WARN("failed to lock task schedule for request handling; closing connection");
        close_network_connection(session->connection);
        return;
    }

    if (!lock_session(session)) {
        RCLOG_WARN("failed to lock session for request; closing connection");
        close_network_connection(session->connection);
        return;
    }

    if (!strcmp("TERM", request->command_name)) {
        handle_termination_request(session, request);
    } else {
        handle_command_request(session, request);
    }

    unlock_session(session);
    unlock_schedule(session->server->config.task_schedule);
}

static error_t new_connection(network_connection_t *connection, void **handler_reference, void *constructor_reference) {
    server_t *server = constructor_reference;
    
    error_t err = create_session((session_t**) handler_reference, connection, server);
    if (err != ERROR_NONE) {
        return err;
    }
    
    session_t *session = *handler_reference;
    session->phase = SESSION_PHASE_AWAIT_VERSION;

    err = register_session(server, session);
    if (err != ERROR_NONE) {
        RCLOG_WARN("failed to register session");
        return err;
    }
    
    err = send_to_network(connection, "XPRC;version,password\n", NETWORK_SEND_COMPLETE_STRING);
    if (err != ERROR_NONE) {
        RCLOG_WARN("failed to send handshake");
        return err;
    }
    
    return ERROR_NONE;
}

static void on_line_received(void *handler_reference, char *line, int length) {
    session_t *session = handler_reference;

    // TODO: accept requested number of lines for handshake, even if empty to comply with spec and don't terminate early
    // TODO: terminate unauthenticated connections after a timeout
    // TODO: introduce random delay in case of failed handshake/authentication (see specification)
    
    if (session->phase == SESSION_PHASE_LOGGED_IN) {
        request_t *request = NULL;
        error_t err = parse_request(&request, line, length);
        if (err != ERROR_NONE) {
            send_or_close(session->connection, "*ERR " INT64_FORMAT " invalid syntax\n", millis_since_reference(session));
            return;
        }

        handle_request(session, request);
        destroy_request(request);
    } else if (session->phase == SESSION_PHASE_FAILED) {
        return;
    } else if (session->phase == SESSION_PHASE_AWAIT_VERSION) {
        if (length < 2 || line[0] != 'v') {
            session->phase = SESSION_PHASE_FAILED;
            RCLOG_WARN("connection failed due to bad syntax on handshake line while expecting version");
            close_network_connection(session->connection);
            return;
        }
            
        session->client_version = atoi(&(line[1]));
        if (num_digits(session->client_version) != (length-1)) {
            RCLOG_WARN("connection failed due to syntactically invalid version number during handshake");
            session->phase = SESSION_PHASE_FAILED;
            close_network_connection(session->connection);
            return;
        }

        session->phase = SESSION_PHASE_AWAIT_PASSWORD;
    } else if (session->phase == SESSION_PHASE_AWAIT_PASSWORD) {
        if (length <= 0) {
            RCLOG_WARN("connection failed due to zero password received");
            session->phase = SESSION_PHASE_FAILED;
            close_network_connection(session->connection);
            return;
        }

        int expected_password_length = strlen(session->server->config.password);
        if (length != expected_password_length || strncmp(line, session->server->config.password, expected_password_length)) {
            RCLOG_WARN("connection failed due to wrong password");
            session->phase = SESSION_PHASE_FAILED;
            close_network_connection(session->connection);
            return;
        }

        if (session->client_version != SERVER_PROTOCOL_VERSION) {
            RCLOG_WARN("connection failed due to client reporting unsupported protocol version %d", session->client_version);
            session->phase = SESSION_PHASE_FAILED;
            send_to_network(session->connection, "v" STR(SERVER_PROTOCOL_VERSION) ";ERR:unsupported protocol version\n", NETWORK_SEND_COMPLETE_STRING);
            close_network_connection(session->connection);
            return;
        }

        if (!timespec_get(&session->reference_time, TIME_UTC)) {
            RCLOG_WARN("connection failed because reference timestamp could not be set");
            session->phase = SESSION_PHASE_FAILED;
            send_to_network(session->connection, "v" STR(SERVER_PROTOCOL_VERSION) ";ERR:internal server error\n", NETWORK_SEND_COMPLETE_STRING);
            close_network_connection(session->connection);
            return;
        }

        session->reference_millis = millis_of_timespec(&session->reference_time);
        if (session->reference_millis < 0) {
            RCLOG_WARN("connection failed because reference timestamp is negative");
            session->phase = SESSION_PHASE_FAILED;
            send_to_network(session->connection, "v" STR(SERVER_PROTOCOL_VERSION) ";ERR:internal server error\n", NETWORK_SEND_COMPLETE_STRING);
            close_network_connection(session->connection);
            return;
        }
        
        char coarse_timestamp[REFERENCE_COARSE_TIMESTAMP_BUFFER_SIZE] = {0};
        if (!strftime(coarse_timestamp, REFERENCE_COARSE_TIMESTAMP_BUFFER_SIZE-1, "%Y-%m-%dT%H:%M:%S", gmtime(&session->reference_time.tv_sec))) {
            RCLOG_WARN("connection failed because reference coarse timestamp could not be formatted");
            session->phase = SESSION_PHASE_FAILED;
            send_to_network(session->connection, "v" STR(SERVER_PROTOCOL_VERSION) ";ERR:internal server error\n", NETWORK_SEND_COMPLETE_STRING);
            close_network_connection(session->connection);
            return;
        }

        int timestamp_millis = session->reference_time.tv_nsec / 1000000;
        
        char handshake_completion_buffer[HANDSHAKE_COMPLETION_BUFFER_SIZE] = {0};
        int res = snprintf(handshake_completion_buffer, HANDSHAKE_COMPLETION_BUFFER_SIZE, "v%d;OK;%s.%03d+00:00\n", SERVER_PROTOCOL_VERSION, coarse_timestamp, timestamp_millis);
        if (res < 0 || res >= HANDSHAKE_COMPLETION_BUFFER_SIZE) {
            RCLOG_WARN("connection failed because handshake completion could not be encoded");
            session->phase = SESSION_PHASE_FAILED;
            send_to_network(session->connection, "v" STR(SERVER_PROTOCOL_VERSION) ";ERR:internal server error\n", NETWORK_SEND_COMPLETE_STRING);
            close_network_connection(session->connection);
            return;
        }

        error_t err = send_to_network(session->connection, handshake_completion_buffer, NETWORK_SEND_COMPLETE_STRING);
        if (err != ERROR_NONE) {
            RCLOG_WARN("connection failed because handshake completion could not be sent");
            session->phase = SESSION_PHASE_FAILED;
            close_network_connection(session->connection);
            return;
        }

        session->phase = SESSION_PHASE_LOGGED_IN;
    } else {
        session->phase = SESSION_PHASE_FAILED;
    }
}

static void on_connection_closing(void *handler_reference) {
    session_t *session = handler_reference;
    
    error_t err = unregister_session(session->server, session);
    if (err != ERROR_NONE) {
        RCLOG_WARN("failed to unregister session from server: %d", err);
    }
    
    destroy_session(session);
}

server_config_t* copy_server_config(server_config_t *source) {
    if (!source) {
        RCLOG_WARN("copy_server_config called with NULL");
        return NULL;
    }

    server_config_t *out = copy_memory(source, sizeof(server_config_t));

    if (out->password) {
        out->password = copy_string(out->password);
        if (!out->password) {
            RCLOG_WARN("[server] failed to copy password in server config");
            free(out);
            return NULL;
        }
    }

    error_t err = copy_network_config_to(&out->network, &out->network);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[server] failed to copy network config in server config");
        if (out->password) {
            free(out->password);
        }
        free(out);
        return NULL;
    }

    return out;
}

void destroy_server_config(server_config_t *config) {
    if (!config) {
        return;
    }

    if (config->password) {
        free(config->password);
        config->password = NULL;
    }

    destroy_network_config_contents(&config->network);

    free(config);
}

error_t start_server(server_t **server, server_config_t *config) {
    if (!server || !config) {
        RCLOG_WARN("start_server missing parameters: server=%p, config=%p", server, config);
        return ERROR_UNSPECIFIC;
    }

    // TODO: check password after copy
    if (!config || !validate_password(config->password)) {
        RCLOG_WARN("bad password, refusing to start");
        return ERROR_UNSPECIFIC;
    }

    *server = zmalloc(sizeof(server_t));
    if (!(*server)) {
        return ERROR_MEMORY_ALLOCATION;
    }

    // recursive mutex is needed because closing connections and maintenance may operate on the list
    // at the same time, requiring duplicate lock by the same thread, see maintain_server
    if (mtx_init(&(*server)->mutex, mtx_plain | mtx_recursive) != thrd_success) {
        return ERROR_UNSPECIFIC;
    }
    
    (*server)->sessions = create_list();
    if (!(*server)->sessions) {
        mtx_destroy(&(*server)->mutex);
        return ERROR_MEMORY_ALLOCATION;
    }

    (*server)->config = *config;
    (*server)->config.password = copy_string(config->password);
    if (!(*server)->config.password) {
        destroy_list((*server)->sessions, NULL);
        mtx_destroy(&(*server)->mutex);
        free(*server);
        *server = NULL;
        return ERROR_MEMORY_ALLOCATION;
    }

    network_handler_t handler = {
        .new_connection = new_connection,
        .new_connection_constructor_reference = *server,
        .on_line_received = on_line_received,
        .on_connection_closing = on_connection_closing,
    };

    error_t err = create_network_server(&(*server)->network, &(*server)->config.network, handler);
    if (err != ERROR_NONE) {
        destroy_list((*server)->sessions, NULL);
        mtx_destroy(&(*server)->mutex);
        free((*server)->config.password);
        free(*server);
        *server = NULL;
        return err;
    }
    
    return ERROR_NONE;
}

error_t stop_server(server_t *server) {
    if (!server) {
        RCLOG_WARN("stop_server called with NULL");
        return ERROR_UNSPECIFIC;
    }

    destroy_network_server(server->network); // TODO: use return value
    destroy_list(server->sessions, NULL);
    mtx_destroy(&server->mutex);
    free(server->config.password);
    free(server);
    return ERROR_NONE;
}

error_t register_session(server_t *server, session_t *session) {
    if (!server || !session) {
        RCLOG_WARN("register_session missing parameters: server=%p, session=%p", server, session);
        return ERROR_UNSPECIFIC;
    }

    if (mtx_lock(&server->mutex) != thrd_success) {
        return ERROR_MUTEX_FAILED;
    }

    list_append(server->sessions, session);

    mtx_unlock(&server->mutex);

    return ERROR_NONE;
}

error_t unregister_session(server_t *server, session_t *session) {
    if (!server || !session) {
        RCLOG_WARN("unregister_session missing parameters: server=%p, session=%p", server, session);
        return ERROR_UNSPECIFIC;
    }

    if (mtx_lock(&server->mutex) != thrd_success) {
        return ERROR_MUTEX_FAILED;
    }

    list_item_t *item = list_find(server->sessions, session);
    if (item) {
        list_delete_item(server->sessions, item, NULL);
    }

    mtx_unlock(&server->mutex);
    
    return item ? ERROR_NONE : ERROR_UNSPECIFIC;
}

static void destroy_pending_channels(channels_table_t *channels) {
    for (int i=0; i<CHANNELS_NUM_SUBTABLES; i++) {
        channels_subtable_t *subtable = channels->subtables[i];
        if (!subtable) {
            continue;
        }

        for (int j=0; j<CHANNELS_SEGMENT; j++) {
            channel_t *channel = subtable->channels[j];
            if (!channel || !channel->destruction_requested) {
                continue;
            }

            if (channel->command && channel->command->destroy) {
                error_t err = channel->command->destroy(channel->command_ref);
                if (err != ERROR_NONE) {
                    RCLOG_WARN("channel command destruction failed (error %d)", err);
                    return;
                }
            }

            // FIXME: potential memleak - pop_channel returns the channel reference, we don't free it
            if (!pop_channel(channels, channel->id)) {
                RCLOG_WARN("failed to remove channel after command destruction");
                return;
            }

            // channel subtable might have been removed, inner loop needs to be aborted in that case
            if (channels->subtables[i] != subtable) {
                break;
            }
        }
    }
}

error_t maintain_server(server_t *server) {
    error_t out_err = ERROR_NONE;

    if (!server) {
        RCLOG_WARN("maintain_server called with NULL");
        return ERROR_UNSPECIFIC;
    }
    
    if (mtx_lock(&server->mutex) != thrd_success) {
        return ERROR_MUTEX_FAILED;
    }

    list_item_t *item = server->sessions->head;
    while (item) {
        // channel commands may trigger closing of connections which could unregister the session while
        // we already process the list, so we need to make sure we don't loose track of list references
        list_item_t *next_item = item->next;
        
        session_t *session = item->value;
        if (!lock_session(session)) {
            out_err = ERROR_MUTEX_FAILED;
        } else {
            destroy_pending_channels(session->channels);
            unlock_session(session);
        }
        
        item = next_item;
    }
    
    mtx_unlock(&server->mutex);
    
    return out_err;
}
