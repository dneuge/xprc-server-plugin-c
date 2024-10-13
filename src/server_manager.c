#include "logger.h"
#include "utils.h"

#include "server_manager.h"

server_manager_t* create_server_manager(settings_manager_t *settings_manager) {
    if (!settings_manager) {
        RCLOG_WARN("[server manager] settings manager missing on creation");
        return ERROR_UNSPECIFIC;
    }

    server_manager_t *out = zalloc(sizeof(server_manager_t));
    if (!out) {
        RCLOG_WARN("[server manager] creation failed (out of memory?)");
        return ERROR_MEMORY_ALLOCATION;
    }

    if (mtx_init(&settings_manager->mutex, mtx_plain | mtx_recursive) != thrd_success) {
        RCLOG_WARN("[server manager] failed to initialize mutex during creation");
        free(out);
        return ERROR_UNSPECIFIC;
    }

    out->settings_manager = settings_manager;

    return out;
}

static error_t lock_server_manager(server_manager_t *server_manager) {
    if (server_manager->destruction_pending) {
        return ERROR_DESTRUCTION_PENDING;
    }

    if (mtx_lock(&server_manager->mutex) != thrd_success) {
        return ERROR_MUTEX_FAILED;
    }

    if (server_manager->destruction_pending) {
        unlock_settings_manager(server_manager);
        return ERROR_DESTRUCTION_PENDING;
    }

    return ERROR_NONE;
}

static void unlock_server_manager(server_manager_t *server_manager) {
    if (mtx_unlock(&server_manager->mutex) != thrd_success) {
        RCLOG_ERROR("[server manager] failed to unlock mutex; proceeding anyway");
    }
}

error_t destroy_server_manager(server_manager_t *server_manager) {
    if (!server_manager) {
        RCLOG_WARN("[server manager] attempted to destroy NULL instance - we let that pass but there's a bug somewhere else");
        return ERROR_NONE;
    }

    if (server_manager->destruction_pending) {
        RCLOG_WARN("[server manager] attempted to destroy an instance that is already pending destruction; aborting early");
        return ERROR_DESTRUCTION_PENDING;
    }

    if (lock_server_manager(server_manager) != ERROR_NONE) {
        RCLOG_WARN("[server manager] failed to lock for destruction; aborting");
        return ERROR_DESTRUCTION_PENDING;
    }

    if (!server_manager->shutdown || server_manager->server) {
        RCLOG_WARN("[server manager] refusing to destroy manager which has not been shut down");
        unlock_server_manager(server_manager);
        return ERROR_UNSPECIFIC;
    }

    if (server_manager->destruction_pending) {
        RCLOG_WARN("[server manager] attempted to destroy an instance that is already pending destruction; aborting late");
        unlock_server_manager(server_manager);
        return ERROR_DESTRUCTION_PENDING;
    }

    server_manager->destruction_pending = true;

    unlock_server_manager(server_manager);

    // lock and unlock again in case another thread accessed the instance in the mean-time
    if (lock_server_manager(server_manager) != ERROR_NONE) {
        RCLOG_WARN("[server manager] failed to lock server manager a second time on destruction; continuing regardless");
    } else {
        unlock_server_manager(server_manager);
    }

    if (server_manager->server_config) {
        destroy_server_config(server_manager->server_config);
        server_manager->server_config = NULL;
    }

    mtx_destroy(&server_manager->mutex);

    free(server_manager);

    return ERROR_NONE;
}

error_t provide_managed_server_base_config(server_manager_t *server_manager, server_config_t *base_config) {
    error_t err = ERROR_NONE;

    if (!server_manager || !base_config) {
        RCLOG_WARN("[server manager] input to provide_managed_server_base_config is missing: server_manager=%p, base_config=%p", server_manager, base_config);
        return ERROR_UNSPECIFIC;
    }

    err = lock_server_manager(server_manager);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[server manager] locking failed; unable to store base config: %d", err);
        return err;
    }

    server_config_t *base_config_copy = copy_server_config(base_config);
    if (!base_config_copy) {
        RCLOG_WARN("[server manager] failed to copy base server configuration");
        err = ERROR_MEMORY_ALLOCATION;
        goto end;
    }

    if (server_manager->server_config) {
        destroy_server_config(server_manager->server_config);
    }

    server_manager->server_config = base_config_copy;

end:
    unlock_server_manager(server_manager);
    return err;
}

static error_t update_wanted_state(server_manager_t *server_manager, bool first, bool next) {
    error_t err = ERROR_NONE;

    err = lock_server_manager(server_manager);
    if (err != ERROR_NONE) {
        return err;
    }

    server_manager->user_intervention_needed = false;

    if (server_manager->shutdown && (first || next)) {
        RCLOG_WARN("[server manager] shutdown pending, requested wanted state %d/%d is invalid", first, next);
        err = ERROR_UNSPECIFIC;
        goto end;
    }

    server_manager->wanted_state_first = first;
    server_manager->wanted_state_next = next;
    server_manager->change_state = true;

end:
    unlock_server_manager(server_manager);

    return err;
}

static error_t update_wanted_state_and_maintain(server_manager_t *server_manager, const char* description, bool first, bool next) {
    error_t err = ERROR_NONE;

    err = update_wanted_state(server_manager, first, next);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[server manager] unable to record wanted state for %s: %d", description, err);
        return err;
    }

    // FIXME: maintain_server_manager may defer actions for next call; call in a loop?
    err = maintain_server_manager(server_manager);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[server manager] maintenance failed after updated state for %s: %d", description, err);
    }

    return err;
}

// FIXME: transition states asynchronously whenever feasible; only call maintenance if that is insufficient
error_t start_managed_server(server_manager_t *server_manager) {
    return update_wanted_state_and_maintain(server_manager, "start", true, true);
}

error_t stop_managed_server(server_manager_t *server_manager) {
    return update_wanted_state_and_maintain(server_manager, "stop", false, false);
}

error_t restart_managed_server(server_manager_t *server_manager) {
    return update_wanted_state_and_maintain(server_manager, "restart", false, true);
}

error_t shutdown_managed_server(server_manager_t *server_manager) {
    server_manager->shutdown = true;
    return update_wanted_state_and_maintain(server_manager, "shutdown", false, false);
}

static error_t update_server_config(server_manager_t *server_manager) {
    error_t err = ERROR_NONE;

    settings_t *settings = create_settings();
    if (!settings) {
        RCLOG_WARN("[server manager] failed to create settings to adapt server configuration");
        goto error;
    }

    err = copy_settings_from_manager(server_manager->settings_manager, settings, SETTINGS_COPY_PASSWORD);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[server manager] failed to copy settings from manager to adapt server configuration: %d", err);
        goto error;
    }

    char *password_copy = copy_string(settings->password);
    if (!password_copy) {
        RCLOG_WARN("[server manager] failed to copy password from settings manager");
        goto error;
    }

    server_config_t *config = server_manager->server_config;

    if (config->password) {
        free(config->password);
    }

    config->password = password_copy;
    config->network.enable_ipv6 = settings->network_enable_ipv6;
    config->network.interface_address = settings->network_interface;
    config->network.port = settings->network_port;

    destroy_settings(settings);

    return ERROR_NONE;

error:
    if (settings) {
        destroy_settings(settings);
    }

    return (err != ERROR_NONE) ? err : ERROR_UNSPECIFIC;
}

static error_t do_server_start(server_manager_t *server_manager) {
    // NOTE: callers must ensure thread-safety

    error_t err = update_server_config(server_manager);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[server manager] failed to build initial server configuration, unable to start; error %d", err);
        server_manager->user_intervention_needed = true;
        return err;
    }

    err = start_server(&server_manager->server, server_manager->server_config);
    if (err != ERROR_NONE) {
        RCLOG_ERROR("[server manager] failed to start server: %d", err);
        return err;
    }

    return ERROR_NONE;
}

static error_t do_server_stop(server_manager_t *server_manager) {
    // NOTE: callers must ensure thread-safety

    if (!server_manager->server) {
        return ERROR_NONE;
    }

    error_t err = stop_server(server_manager->server);
    if (err != ERROR_NONE) {
        RCLOG_ERROR("[server manager] server failed to stop: %d", err);
        return err;
    }

    server_manager->server = NULL;

    return ERROR_NONE;
}

error_t maintain_server_manager(server_manager_t *server_manager) {
    error_t err = ERROR_NONE;

    err = lock_server_manager(server_manager);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[server manager] maintenance failed: unable to lock manager");
        return err;
    }

    if (server_manager->server) {
        RCLOG_TRACE("[server manager] performing maintenance on server");
        err = maintain_server(server_manager->server);
        if (err != ERROR_NONE) {
            RCLOG_WARN("[server manager] server maintenance failed: error %d", err);
        }
    }

    // we only do state update handling from here on, skip if no changes have been requested
    if (!server_manager->change_state) {
        goto end;
    }

    bool current_state = (server_manager->server != NULL);
    bool current_state_matches_first = (current_state == server_manager->wanted_state_first);

    RCLOG_DEBUG("[server manager] state update: current=%d, first=%d, next=%d", current_state, server_manager->wanted_state_first, server_manager->wanted_state_next);

    if (current_state_matches_first) {
        // move on to next state
        RCLOG_DEBUG("[server manager] first state already reached (%d), forwarding to next (%d)", current_state, server_manager->wanted_state_next);
        server_manager->wanted_state_first = server_manager->wanted_state_next;
        current_state_matches_first = (current_state == server_manager->wanted_state_first);
    }

    bool wanted_state_toggles = server_manager->wanted_state_first != server_manager->wanted_state_next;
    if (!wanted_state_toggles && current_state_matches_first) {
        // nothing to do, stable state reached
        RCLOG_DEBUG("[server manager] state transitions completed early");
        server_manager->change_state = false;
        goto end;
    }

    RCLOG_DEBUG("[server manager] transitioning to %d", server_manager->wanted_state_first);
    if (server_manager->wanted_state_first) {
        err = do_server_start(server_manager);
    } else {
        err = do_server_stop(server_manager);
    }

    if (err != ERROR_NONE) {
        // TODO: stop retrying after a consecutive number of errors?
        // TODO: notify user/UI
        RCLOG_WARN("[server manager] state transition failed (%d => %d), will retry: %d", current_state, server_manager->wanted_state_first, err);

        if (server_manager->user_intervention_needed) {
            // TODO: inform user via popup to check settings if startup had been requested
            RCLOG_ERROR("[server manager] giving up on state transitions, user intervention is needed");
            server_manager->change_state = false;
        }

        goto end;
    }

    RCLOG_DEBUG("[server manager] first state transition successful");
    if (!wanted_state_toggles) {
        RCLOG_DEBUG("[server manager] state transitions completed");
        server_manager->change_state = false;
    }

end:
    unlock_server_manager(server_manager);
    return err;
}

bool is_running_server_state(managed_server_state_t state) {
    return state == STARTED || state == RESTARTING;
}

managed_server_state_t get_managed_server_state(server_manager_t *server_manager) {
    if (!server_manager) {
        RCLOG_WARN("[server manager] get_managed_server_state called with NULL");
        return UNKNOWN;
    }

    error_t err = lock_server_manager(server_manager);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[server manager] get_managed_server_state failed to acquire lock: %d", err);
        return UNKNOWN;
    }

    managed_server_state_t state = UNKNOWN;
    if (server_manager->shutdown) {
        state = SHUTDOWN;
    } else if (server_manager->server) {
        state = STARTED;
    } else if (server_manager->change_state && (server_manager->wanted_state_first || server_manager->wanted_state_next)) {
        state = RESTARTING;
    } else {
        state = STOPPED;
    }

    unlock_server_manager(server_manager);

    return state;
}

