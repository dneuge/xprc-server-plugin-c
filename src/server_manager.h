#ifndef SERVER_MANAGER_H
#define SERVER_MANAGER_H

#include "server.h"
#include "settings_manager.h"

typedef enum {
    UNKNOWN = 0,
    STOPPED,
    STARTED,
    RESTARTING,
    SHUTDOWN
} managed_server_state_t;

bool is_running_server_state(managed_server_state_t state);

typedef struct {
    settings_manager_t *settings_manager;
    server_config_t *server_config;

    server_t *server;
    bool user_intervention_needed;
    bool change_state;
    bool wanted_state_first;
    bool wanted_state_next;
    bool shutdown;
    bool destruction_pending;

    mtx_t mutex;
} server_manager_t;

server_manager_t* create_server_manager(settings_manager_t *settings_manager);
error_t destroy_server_manager(server_manager_t *server_manager);

error_t provide_managed_server_base_config(server_manager_t *server_manager, server_config_t *server_config);
error_t maintain_server_manager(server_manager_t *server_manager);

error_t start_managed_server(server_manager_t *server_manager);
error_t stop_managed_server(server_manager_t *server_manager);
error_t restart_managed_server(server_manager_t *server_manager);
error_t shutdown_managed_server(server_manager_t *server_manager);

managed_server_state_t get_managed_server_state(server_manager_t *server_manager);


#endif
