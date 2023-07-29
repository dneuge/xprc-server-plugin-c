#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "requests.h"
#include "server.h"
#include "session.h"
#include "utils.h"

#define _STR(x) #x
#define STR(x) _STR(x)

#define SERVER_PROTOCOL_VERSION 1

#define REFERENCE_COARSE_TIMESTAMP_BUFFER_SIZE 30
#define HANDSHAKE_COMPLETION_BUFFER_SIZE 50

static void send_or_close(network_connection_t *connection, char *format, ...) {
    va_list args;
    va_start(args, format);
    char *msg = dynamic_vsprintf(format, args);
    va_end(args);
    
    if (!msg) {
        printf("[XPRC] terminating connection because formatting channel message failed: %s\n", format);
        close_network_connection(connection);
        return;
    }
    
    error_t err = send_to_network(connection, msg, NETWORK_SEND_COMPLETE_STRING);
    if (err != ERROR_NONE) {
        printf("[XPRC] terminating connection because sending channel message failed: %s\n", msg);
        close_network_connection(connection);
    }

    free(msg);
}

static void handle_command_request(session_t *session, request_t *request) {
    char channel_name[5] = {0};
    channel_id_to_string(request->channel_id, channel_name);

    if (has_channel(session->channels, request->channel_id)) {
        send_or_close(session->connection, "-ERR %s %ld channel busy\n", channel_name, millis_since_reference(session));
        return;
    }

    channel_t *channel = zalloc(sizeof(channel_t));
    if (!channel) {
        send_or_close(session->connection, "-ERR %s %ld channel creation failed\n", channel_name, millis_since_reference(session));
        return;
    }

    channel->id = request->channel_id;
    channel->state = CHANNEL_STATE_INITIAL;
    if (!put_channel(session->channels, channel)) {
        send_or_close(session->connection, "-ERR %s %ld channel registration failed\n", channel_name, millis_since_reference(session));
        return;
    }
    
    error_t err = create_command(session->server->config.command_factory, channel, session, request);
    if (err != ERROR_NONE) {
        // use session error_channel function instead of raw send_or_close/send_to_network as the channel might have been closed
        // already by the command that just failed to be created
        err = error_channel(session, channel->id, CURRENT_TIME_REFERENCE, "command creation failed");
        if (err != ERROR_NONE && err != SESSION_ERROR_INVALID_CHANNEL_STATE) {
            printf("[XPRC] terminating connection because sending channel error failed after failed command creation\n");
            close_network_connection(session->connection);
            return;
        }
        
        if (!pop_channel(session->channels, channel->id)) {
            printf("[XPRC] failed to clear dead channel %s after command creation failed, closing connection\n", channel_name);
            close_network_connection(session->connection);
        }
    }
}

static void handle_termination_request(session_t *session, request_t *request) {
    char channel_name[5] = {0};
    channel_id_to_string(request->channel_id, channel_name);
    
    channel_t *channel = get_channel(session->channels, request->channel_id);
    if (!channel || channel->state == CHANNEL_STATE_CLOSED || channel->destruction_requested) {
        send_or_close(session->connection, "-ERR %s %ld channel does not exist\n", channel_name, millis_since_reference(session));
        return;
    }

    error_t err = channel->command->terminate(channel->command_ref);
    if (err != ERROR_NONE) {
        printf("[XPRC] command termination for channel %s failed (error %d), closing connection\n", channel_name, err);
        close_network_connection(session->connection);
    }
}    

static void handle_request(session_t *session, request_t *request) {
    if (!lock_session(session)) {
        printf("[XPRC] failed to lock session for request; closing connection\n");
        close_network_connection(session->connection);
        return;
    }

    if (!strcmp("TERM", request->command_name)) {
        handle_termination_request(session, request);
    } else {
        handle_command_request(session, request);
    }

    unlock_session(session);
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
        printf("[XPRC] failed to register session \n");
        return err;
    }
    
    err = send_to_network(connection, "XPRC;version,password\n", NETWORK_SEND_COMPLETE_STRING);
    if (err != ERROR_NONE) {
        printf("[XPRC] failed to send handshake\n");
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
            send_or_close(session->connection, "*ERR %ld invalid syntax\n", millis_since_reference(session));
            return;
        }

        handle_request(session, request);
        destroy_request(request);
    } else if (session->phase == SESSION_PHASE_FAILED) {
        return;
    } else if (session->phase == SESSION_PHASE_AWAIT_VERSION) {
        if (length < 2 || line[0] != 'v') {
            session->phase = SESSION_PHASE_FAILED;
            printf("[XPRC] connection failed due to bad syntax on handshake line while expecting version\n");
            close_network_connection(session->connection);
            return;
        }
            
        session->client_version = atoi(&(line[1]));
        if (num_digits(session->client_version) != (length-1)) {
            printf("[XPRC] connection failed due to syntactically invalid version number during handshake\n");
            session->phase = SESSION_PHASE_FAILED;
            close_network_connection(session->connection);
            return;
        }

        session->phase = SESSION_PHASE_AWAIT_PASSWORD;
    } else if (session->phase == SESSION_PHASE_AWAIT_PASSWORD) {
        if (length <= 0) {
            printf("[XPRC] connection failed due to zero password received\n");
            session->phase = SESSION_PHASE_FAILED;
            close_network_connection(session->connection);
            return;
        }

        int expected_password_length = strlen(session->server->config.password);
        if (length != expected_password_length || strncmp(line, session->server->config.password, expected_password_length)) {
            printf("[XPRC] connection failed due to wrong password\n");
            session->phase = SESSION_PHASE_FAILED;
            close_network_connection(session->connection);
            return;
        }

        if (session->client_version != SERVER_PROTOCOL_VERSION) {
            printf("[XPRC] connection failed due to client reporting unsupported protocol version %d\n", session->client_version);
            session->phase = SESSION_PHASE_FAILED;
            send_to_network(session->connection, "v" STR(SERVER_PROTOCOL_VERSION) ";ERR:unsupported protocol version\n", NETWORK_SEND_COMPLETE_STRING);
            close_network_connection(session->connection);
            return;
        }

        if (!timespec_get(&session->reference_time, TIME_UTC)) {
            printf("[XPRC] connection failed because reference timestamp could not be set\n");
            session->phase = SESSION_PHASE_FAILED;
            send_to_network(session->connection, "v" STR(SERVER_PROTOCOL_VERSION) ";ERR:internal server error\n", NETWORK_SEND_COMPLETE_STRING);
            close_network_connection(session->connection);
            return;
        }

        session->reference_millis = millis_of_timespec(&session->reference_time);
        if (session->reference_millis < 0) {
            printf("[XPRC] connection failed because reference timestamp is negative\n");
            session->phase = SESSION_PHASE_FAILED;
            send_to_network(session->connection, "v" STR(SERVER_PROTOCOL_VERSION) ";ERR:internal server error\n", NETWORK_SEND_COMPLETE_STRING);
            close_network_connection(session->connection);
            return;
        }
        
        char coarse_timestamp[REFERENCE_COARSE_TIMESTAMP_BUFFER_SIZE] = {0};
        if (!strftime(coarse_timestamp, REFERENCE_COARSE_TIMESTAMP_BUFFER_SIZE-1, "%Y-%m-%dT%H:%M:%S", gmtime(&session->reference_time.tv_sec))) {
            printf("[XPRC] connection failed because reference coarse timestamp could not be formatted\n");
            session->phase = SESSION_PHASE_FAILED;
            send_to_network(session->connection, "v" STR(SERVER_PROTOCOL_VERSION) ";ERR:internal server error\n", NETWORK_SEND_COMPLETE_STRING);
            close_network_connection(session->connection);
            return;
        }

        int timestamp_millis = session->reference_time.tv_nsec / 1000000;
        
        char handshake_completion_buffer[HANDSHAKE_COMPLETION_BUFFER_SIZE] = {0};
        int res = snprintf(handshake_completion_buffer, HANDSHAKE_COMPLETION_BUFFER_SIZE, "v%d;OK;%s.%03d+00:00\n", SERVER_PROTOCOL_VERSION, coarse_timestamp, timestamp_millis);
        if (res < 0 || res >= HANDSHAKE_COMPLETION_BUFFER_SIZE) {
            printf("[XPRC] connection failed because handshake completion could not be encoded\n");
            session->phase = SESSION_PHASE_FAILED;
            send_to_network(session->connection, "v" STR(SERVER_PROTOCOL_VERSION) ";ERR:internal server error\n", NETWORK_SEND_COMPLETE_STRING);
            close_network_connection(session->connection);
            return;
        }

        error_t err = send_to_network(session->connection, handshake_completion_buffer, NETWORK_SEND_COMPLETE_STRING);
        if (err != ERROR_NONE) {
            printf("[XPRC] connection failed because handshake completion could not be sent\n");
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
        printf("[XPRC] failed to unregister session from server: %d\n", err);
    }
    
    destroy_session(session);
}

error_t start_server(server_t **server, server_config_t *config) {
    *server = malloc(sizeof(server_t));
    if (!(*server)) {
        return ERROR_MEMORY_ALLOCATION;
    }

    memset(*server, 0, sizeof(server_t));

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
    destroy_network_server(server->network); // TODO: use return value
    destroy_list(server->sessions, NULL);
    mtx_destroy(&server->mutex);
    free(server->config.password);
    free(server);
    return ERROR_NONE;
}

error_t register_session(server_t *server, session_t *session) {
    if (mtx_lock(&server->mutex) != thrd_success) {
        return ERROR_LOCK_FAILED;
    }

    list_append(server->sessions, session);

    mtx_unlock(&server->mutex);

    return ERROR_NONE;
}

error_t unregister_session(server_t *server, session_t *session) {
    if (mtx_lock(&server->mutex) != thrd_success) {
        return ERROR_LOCK_FAILED;
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
                    printf("[XPRC] channel command destruction failed (error %d)\n", err);
                    return;
                }
            }

            if (!pop_channel(channels, channel->id)) {
                printf("[XPRC] failed to remove channel after command destruction\n");
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
    
    if (mtx_lock(&server->mutex) != thrd_success) {
        return ERROR_LOCK_FAILED;
    }

    list_item_t *item = server->sessions->head;
    while (item) {
        // channel commands may trigger closing of connections which could unregister the session while
        // we already process the list, so we need to make sure we don't loose track of list references
        list_item_t *next_item = item->next;
        
        session_t *session = item->value;
        if (!lock_session(session)) {
            out_err = ERROR_LOCK_FAILED;
        } else {
            destroy_pending_channels(session->channels);
            unlock_session(session);
        }
        
        item = next_item;
    }
    
    mtx_unlock(&server->mutex);
    
    return out_err;
}
