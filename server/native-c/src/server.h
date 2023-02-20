#ifndef SERVER_H
#define SERVER_H

#include <stdbool.h>
#include <threads.h>

typedef struct _server_t server_t;

#include "commands.h"
#include "lists.h"
#include "network.h"
#include "task_schedule.h"

typedef struct {
    char *password;
    network_server_config_t network;
    task_schedule_t *task_schedule;
    command_factory_t *command_factory;
} server_config_t;

typedef struct _server_t {
    server_config_t config;
    network_server_t *network;
} server_t;

error_t start_server(server_t **server, server_config_t *config);
error_t stop_server(server_t *server);

#endif
