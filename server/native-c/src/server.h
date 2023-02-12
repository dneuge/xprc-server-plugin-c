#ifndef SERVER_H
#define SERVER_H

#include <stdbool.h>
#include <threads.h>

#include "lists.h"
#include "network.h"

typedef struct {
    char *password;
    network_server_config_t network;
    
    mtx_t *task_queue_mutex;
    prealloc_list_t *task_queue_before_flight_model;
    prealloc_list_t *task_queue_after_flight_model;
} server_config_t;

typedef struct {
    server_config_t config;
    network_server_t *network;
} server_t;

error_t start_server(server_t **server, server_config_t *config);
error_t stop_server(server_t *server);

#endif
