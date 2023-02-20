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
#define INVALID_SYNTAX_ERROR_BUFFER_SIZE 50
#define CHANNEL_IN_USE_ERROR_BUFFER_SIZE 50
#define CHANNEL_FAILED_ERROR_BUFFER_SIZE 50
#define CHANNEL_REGISTRATION_ERROR_BUFFER_SIZE 50
#define COMMAND_FAILED_ERROR_BUFFER_SIZE 50

static void handle_request(session_t *session, request_t *request) {
    char channel_name[5] = {0};
    channel_id_to_string(request->channel_id, channel_name);

    // TODO: support TERM
    
    if (has_channel(session->channels, request->channel_id)) {
        char buffer[CHANNEL_IN_USE_ERROR_BUFFER_SIZE] = {0};
        int res = snprintf(buffer, CHANNEL_IN_USE_ERROR_BUFFER_SIZE, "-ERR %s %ld channel busy\n", channel_name, millis_since_reference(session));
        if (res < 0 || res >= CHANNEL_IN_USE_ERROR_BUFFER_SIZE) {
            printf("[XPRC] terminating connection because formatting channel busy error message failed\n");
            close_network_connection(session->connection);
        }
        return;
    }

    channel_t *channel = zalloc(sizeof(channel_t));
    if (!channel) {
        char buffer[CHANNEL_FAILED_ERROR_BUFFER_SIZE] = {0};
        int res = snprintf(buffer, CHANNEL_FAILED_ERROR_BUFFER_SIZE, "-ERR %s %ld channel creation failed\n", channel_name, millis_since_reference(session));
        if (res < 0 || res >= CHANNEL_FAILED_ERROR_BUFFER_SIZE) {
            printf("[XPRC] terminating connection because formatting channel creation error message failed\n");
            close_network_connection(session->connection);
        }
        return;
    }

    channel->id = request->channel_id;
    channel->state = CHANNEL_STATE_INITIAL;
    if (!put_channel(session->channels, channel)) {
        char buffer[CHANNEL_REGISTRATION_ERROR_BUFFER_SIZE] = {0};
        int res = snprintf(buffer, CHANNEL_REGISTRATION_ERROR_BUFFER_SIZE, "-ERR %s %ld channel registration failed\n", channel_name, millis_since_reference(session));
        if (res < 0 || res >= CHANNEL_REGISTRATION_ERROR_BUFFER_SIZE) {
            printf("[XPRC] terminating connection because formatting channel registration error message failed\n");
            close_network_connection(session->connection);
        }
        return;
    }
    
    error_t err = create_command(session->server->config.command_factory, channel, session, request);
    if (err != ERROR_NONE) {
        char buffer[COMMAND_FAILED_ERROR_BUFFER_SIZE] = {0};
        int res = snprintf(buffer, COMMAND_FAILED_ERROR_BUFFER_SIZE, "-ERR %s %ld command creation failed\n", channel_name, millis_since_reference(session));
        if (res < 0 || res >= COMMAND_FAILED_ERROR_BUFFER_SIZE) {
            printf("[XPRC] terminating connection because formatting command creation error message failed\n");
            close_network_connection(session->connection);
        }
        return;
    }
}

static error_t new_connection(network_connection_t *connection, void **handler_reference, void *constructor_reference) {
    server_t *server = constructor_reference;
    
    error_t err = create_session((session_t**) handler_reference, connection, server);
    if (err != ERROR_NONE) {
        return err;
    }
    
    session_t *session = *handler_reference;
    session->phase = SESSION_PHASE_AWAIT_VERSION;
    
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
            char buffer[INVALID_SYNTAX_ERROR_BUFFER_SIZE] = {0};
            int res = snprintf(buffer, INVALID_SYNTAX_ERROR_BUFFER_SIZE, "*ERR %ld invalid syntax\n", millis_since_reference(session));
            if (res < 0 || res >= INVALID_SYNTAX_ERROR_BUFFER_SIZE) {
                printf("[XPRC] terminating connection because formatting invalid syntax error message failed\n");
                close_network_connection(session->connection);
            }

            error_t err = send_to_network(session->connection, buffer, NETWORK_SEND_COMPLETE_STRING);
            if (err != ERROR_NONE) {
                printf("[XPRC] terminating connection because sending invalid syntax error message failed\n");
                close_network_connection(session->connection);
            }
            
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
    destroy_session(session);
}

error_t start_server(server_t **server, server_config_t *config) {
    *server = malloc(sizeof(server_t));
    if (!(*server)) {
        return ERROR_MEMORY_ALLOCATION;
    }

    memset(*server, 0, sizeof(server_t));

    (*server)->config = *config;
    (*server)->config.password = copy_string(config->password);
    if (!(*server)->config.password) {
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
        free((*server)->config.password);
        free(*server);
        *server = NULL;
        return err;
    }
    
    return ERROR_NONE;
}

error_t stop_server(server_t *server) {
    destroy_network_server(server->network);
    free(server->config.password);
    free(server);
    return ERROR_NONE;
}

