#ifndef SERVER_H
#define SERVER_H

#include <stdbool.h>
#include <threads.h>

typedef struct _server_t server_t;

#include "commands.h"
#include "lists.h"
#include "network.h"
#include "task_schedule.h"

#ifdef BUILD_PLUGIN
#include "dataproxy.h"
#else
// in case we are not building the plugin (but test code/utils)
// we need to stub unused type definitions instead of importing
// plugin code to break compile-time dependency on those files
typedef struct {} dataproxy_registry_t;
#endif

typedef struct {
    char *password;
    network_server_config_t network;
    task_schedule_t *task_schedule;
    command_factory_t *command_factory;
    dataproxy_registry_t *dataproxy_registry;
} server_config_t;

typedef struct _server_t {
    server_config_t config;
    network_server_t *network;
    list_t *sessions;
    mtx_t mutex;
} server_t;

error_t start_server(server_t **server, server_config_t *config);
error_t stop_server(server_t *server);

error_t maintain_server(server_t *server);

error_t register_session(server_t *server, session_t *session);
error_t unregister_session(server_t *server, session_t *session);

#endif
