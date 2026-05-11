#include "fileio.h"
#include "logger.h"
#include "password.h"
#include "utils.h"

#include "settings_manager.h"

#define PASSWORD_FILENAME "password.cfg"
#define PORT_FILENAME     "port.cfg"
#define SETTINGS_FILENAME "settings.cfg"

settings_manager_t* create_settings_manager(char *xprc_directory, char *server_directory) {
    if (!xprc_directory) {
        RCLOG_WARN("[settings manager] create_settings_manager called with NULL");
        return NULL;
    }

    settings_manager_t *settings_manager = zmalloc(sizeof(settings_manager_t));
    if (!settings_manager) {
        return NULL;
    }

    settings_manager->settings = create_settings();
    if (!settings_manager->settings) {
        goto error;
    }

    settings_manager->xprc_directory = copy_string(xprc_directory);
    if (!settings_manager->xprc_directory) {
        goto error;
    }

    settings_manager->server_directory = copy_string(server_directory);
    if (!settings_manager->server_directory) {
        goto error;
    }

    settings_manager->password_filepath = dynamic_sprintf("%s%c%s", settings_manager->xprc_directory, DIRECTORY_SEPARATOR, PASSWORD_FILENAME);
    if (!settings_manager->password_filepath) {
        goto error;
    }

    settings_manager->port_filepath = dynamic_sprintf("%s%c%s", settings_manager->xprc_directory, DIRECTORY_SEPARATOR, PORT_FILENAME);
    if (!settings_manager->port_filepath) {
        goto error;
    }

    settings_manager->settings_filepath = dynamic_sprintf("%s%c%s", settings_manager->server_directory, DIRECTORY_SEPARATOR, SETTINGS_FILENAME);
    if (!settings_manager->settings_filepath) {
        goto error;
    }

    if (mtx_init(&settings_manager->mutex, mtx_plain | mtx_recursive) == thrd_success) {
        return settings_manager;
    }

    error:
    if (settings_manager->settings_filepath) {
        free(settings_manager->settings_filepath);
    }
    if (settings_manager->password_filepath) {
        free(settings_manager->password_filepath);
    }
    if (settings_manager->port_filepath) {
        free(settings_manager->port_filepath);
    }
    if (settings_manager->server_directory) {
        free(settings_manager->server_directory);
    }
    if (settings_manager->xprc_directory) {
        free(settings_manager->xprc_directory);
    }
    if (settings_manager->settings) {
        destroy_settings(settings_manager->settings);
    }
    free(settings_manager);
    return NULL;
}

error_t destroy_settings_manager(settings_manager_t *settings_manager) {
    error_t err = ERROR_NONE;

    if (!settings_manager) {
        return ERROR_NONE;
    }

    err = lock_settings_manager(settings_manager);
    if (err != ERROR_NONE) {
        return err;
    }

    settings_manager->destruction_pending = true;
    unlock_settings_manager(settings_manager);

    // lock & unlock once more to make sure every thread had a chance to notice pending destruction
    if (mtx_lock(&settings_manager->mutex) != thrd_success) {
        RCLOG_WARN("failed to lock settings manager mutex during destruction; continuing regardless");
    } else {
        mtx_unlock(&settings_manager->mutex);
    }

    destroy_settings(settings_manager->settings);
    settings_manager->settings = NULL;

    if (settings_manager->password_filepath) {
        free(settings_manager->password_filepath);
        settings_manager->password_filepath = NULL;
    }

    if (settings_manager->port_filepath) {
        free(settings_manager->port_filepath);
        settings_manager->port_filepath = NULL;
    }

    if (settings_manager->settings_filepath) {
        free(settings_manager->settings_filepath);
        settings_manager->settings_filepath = NULL;
    }

    if (settings_manager->server_directory) {
        free(settings_manager->server_directory);
        settings_manager->server_directory = NULL;
    }

    if (settings_manager->xprc_directory) {
        free(settings_manager->xprc_directory);
        settings_manager->xprc_directory = NULL;
    }

    mtx_destroy(&settings_manager->mutex);

    free(settings_manager);

    return ERROR_NONE;
}

error_t lock_settings_manager(settings_manager_t *settings_manager) {
    if (!settings_manager) {
        RCLOG_WARN("[settings manager] lock_settings_manager called with NULL");
        return ERROR_UNSPECIFIC;
    }

    if (settings_manager->destruction_pending) {
        return ERROR_DESTRUCTION_PENDING;
    }

    if (mtx_lock(&settings_manager->mutex) != thrd_success) {
        return ERROR_MUTEX_FAILED;
    }

    if (!settings_manager->destruction_pending) {
        return ERROR_NONE;
    }

    unlock_settings_manager(settings_manager);
    return ERROR_DESTRUCTION_PENDING;
}

void unlock_settings_manager(settings_manager_t *settings_manager) {
    if (!settings_manager) {
        RCLOG_WARN("[settings manager] unlock_settings_manager called with NULL");
        return;
    }

    mtx_unlock(&settings_manager->mutex);
}

error_t configure_settings_manager_from_storage(settings_manager_t *settings_manager) {
    error_t err = ERROR_NONE;
    error_t out_err = ERROR_NONE;

    if (!settings_manager) {
        RCLOG_WARN("[settings manager] configure_settings_manager_from_storage called with NULL");
        return ERROR_UNSPECIFIC;
    }

    RCLOG_TRACE("[settings manager] configure_settings_manager_from_storage: locking");
    err = lock_settings_manager(settings_manager);
    if (err != ERROR_NONE) {
        return err;
    }

    RCLOG_TRACE("[settings manager] configure_settings_manager_from_storage: check if settings file exists");
    bool settings_file_exists = check_file_exists(settings_manager->settings_filepath);
    RCLOG_TRACE("[settings manager] configure_settings_manager_from_storage: check if password file exists");
    bool password_file_exists = check_file_exists(settings_manager->password_filepath);
    RCLOG_TRACE("[settings manager] configure_settings_manager_from_storage: check if port file exists");
    bool port_file_exists = check_file_exists(settings_manager->port_filepath);

    bool failed_settings_load = false;
    bool failed_settings_save = false;
    bool failed_password_load = false;
    bool failed_password_save = false;
    bool failed_port_load = false;
    bool failed_port_save = false;
    bool is_password_valid = false;
    bool is_port_valid = false;

    bool is_first_initialization = (!settings_file_exists && !password_file_exists && !port_file_exists);
    RCLOG_TRACE("[settings manager] configure_settings_manager_from_storage: is_first_initialization=%d", is_first_initialization);
    if (is_first_initialization) {
        err = ensure_directory_exists(settings_manager->xprc_directory);
        if (err != ERROR_NONE) {
            RCLOG_WARN("[settings manager] configure_settings_manager_from_storage: failed to ensure existence of XPRC directory (%d) before saving settings on first initialization: %s", err, settings_manager->xprc_directory);
            failed_settings_save = true;
        }

        if (!failed_settings_save) {
            err = ensure_directory_exists(settings_manager->server_directory);
            if (err != ERROR_NONE) {
                RCLOG_WARN("[settings manager] configure_settings_manager_from_storage: failed to ensure existence of server-specific directory (%d) before saving settings on first initialization: %s", err, settings_manager->server_directory);
                failed_settings_save = true;
            }
        }

        if (!failed_settings_save) {
            RCLOG_TRACE("[settings manager] configure_settings_manager_from_storage: saving server-specific settings");
            err = save_server_settings(settings_manager->settings, settings_manager->settings_filepath);
            if (err != ERROR_NONE) {
                RCLOG_WARN("[settings manager] failed to save server-specific settings to %s: %d", settings_manager->settings_filepath, err);
                failed_settings_save = true;
            }
        }
    } else {
        RCLOG_TRACE("[settings manager] configure_settings_manager_from_storage: loading server-specific settings");
        err = load_server_settings(settings_manager->settings, settings_manager->settings_filepath);
        if (err != ERROR_NONE) {
            RCLOG_WARN("[settings manager] failed to load server-specific settings from %s: %d", settings_manager->settings_filepath, err);
            failed_settings_load = true;
        }
    }

    bool should_regenerate_password = settings_manager->settings->auto_regen_password;
    RCLOG_TRACE("[settings manager] configure_settings_manager_from_storage: should_regenerate_password=%d", should_regenerate_password);
    if (should_regenerate_password) {
        // password has already been generated by default initialization but still needs to be saved
        err = ensure_directory_exists(settings_manager->xprc_directory);
        if (err != ERROR_NONE) {
            RCLOG_WARN("[settings manager] configure_settings_manager_from_storage: failed to ensure existence of XPRC directory (%d) before saving regenerated password: %s", err, settings_manager->xprc_directory);
            failed_password_save = true;
        } else {
            RCLOG_TRACE("[settings manager] configure_settings_manager_from_storage: saving password");
            err = save_password(settings_manager->settings, settings_manager->password_filepath);
            if (err != ERROR_NONE) {
                RCLOG_WARN("[settings manager] failed to save password to %s: %d", settings_manager->password_filepath, err);
                failed_password_save = true;
            }
        }
    } else {
        RCLOG_TRACE("[settings manager] configure_settings_manager_from_storage: loading password");
        err = load_password(settings_manager->settings, settings_manager->password_filepath);
        if (err != ERROR_NONE) {
            RCLOG_WARN("[settings manager] failed to load password from %s: %d", settings_manager->password_filepath, err);
            failed_password_load = true;
        }
    }

    RCLOG_TRACE("[settings manager] configure_settings_manager_from_storage: validating password");
    is_password_valid = validate_password(settings_manager->settings->password); // FIXME: why do we take it over if it is invalid?!
    RCLOG_TRACE("[settings manager] configure_settings_manager_from_storage: is_password_valid=%d", is_password_valid);

    if (port_file_exists) {
        err = load_port(settings_manager->settings, settings_manager->port_filepath);
        if (err != ERROR_NONE) {
            RCLOG_WARN("[settings manager] failed to load port from %s: %d", settings_manager->port_filepath, err);
            failed_port_load = true;
        }
    }

    if (!port_file_exists || failed_port_load) {
        err = ensure_directory_exists(settings_manager->xprc_directory);
        if (err != ERROR_NONE) {
            RCLOG_WARN("[settings manager] configure_settings_manager_from_storage: failed to ensure existence of XPRC directory (%d) before saving port: %s", err, settings_manager->xprc_directory);
        } else {
            RCLOG_TRACE("[settings manager] configure_settings_manager_from_storage: saving port");
            err = save_port(settings_manager->settings, settings_manager->port_filepath);
            if (err != ERROR_NONE) {
                RCLOG_WARN("[settings manager] failed to save port to %s: %d", settings_manager->port_filepath, err);
                failed_port_save = true;
            }
        }
    }

    unlock_settings_manager(settings_manager);

    if (failed_settings_load) {
        RCLOG_WARN("[settings manager] failed to load settings");
        return SETTINGS_MANAGER_ERROR_SETTINGS_REVERTED;
    } else if (!is_password_valid) {
        RCLOG_WARN("[settings manager] loaded password is invalid");
        return SETTINGS_MANAGER_ERROR_PASSWORD_INVALID;
    } else if (failed_password_load) {
        RCLOG_WARN("[settings manager] failed to load password");
        return SETTINGS_MANAGER_ERROR_PASSWORD_NOT_LOADED;
    } else if (failed_settings_save) {
        RCLOG_WARN("[settings manager] failed to save settings");
        return SETTINGS_MANAGER_ERROR_SETTINGS_NOT_SAVED;
    } else if (failed_password_save) {
        RCLOG_WARN("[settings manager] failed to save password");
        return SETTINGS_MANAGER_ERROR_PASSWORD_NOT_SAVED;
    } else if (failed_port_save) {
        RCLOG_WARN("[settings manager] failed to save port");
        return SETTINGS_MANAGER_ERROR_PORT_NOT_SAVED;
    } else if (is_first_initialization) {
        RCLOG_WARN("[settings manager] no previous settings found; initialized with defaults");
        return SETTINGS_MANAGER_ERROR_NO_PREVIOUS_SETTINGS;
    } else {
        RCLOG_DEBUG("[settings manager] configured from persistence");
        return ERROR_NONE;
    }
}

error_t persist_settings_from_manager(settings_manager_t *settings_manager) {
    error_t err = ERROR_NONE;
    error_t out_err = ERROR_NONE;

    if (!settings_manager) {
        RCLOG_WARN("[settings manager] persist_settings_from_manager called with NULL");
        return ERROR_UNSPECIFIC;
    }

    err = lock_settings_manager(settings_manager);
    if (err != ERROR_NONE) {
        return err;
    }

    err = ensure_directory_exists(settings_manager->xprc_directory);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[settings manager] failed to ensure existence of XPRC directory (%d): %s", err, settings_manager->xprc_directory);
        out_err = ERROR_UNSPECIFIC;
        goto end;
    }

    err = ensure_directory_exists(settings_manager->server_directory);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[settings manager] failed to ensure existence of server-specific directory (%d): %s", err, settings_manager->server_directory);
        out_err = ERROR_UNSPECIFIC;
        goto end;
    }

    err = save_password(settings_manager->settings, settings_manager->password_filepath);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[settings manager] failed to save password to %s: %d", settings_manager->password_filepath, err);
        out_err = err;
    }

    err = save_port(settings_manager->settings, settings_manager->port_filepath);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[settings manager] failed to save port to %s: %d", settings_manager->port_filepath, err);
        out_err = err;
    }

    err = save_server_settings(settings_manager->settings, settings_manager->settings_filepath);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[settings manager] failed to save settings to %s: %d", settings_manager->settings_filepath, err);
        out_err = err;
    }

end:
    unlock_settings_manager(settings_manager);

    return out_err;
}

error_t copy_settings_to_manager(settings_manager_t *settings_manager, settings_t *settings, bool copy_password) {
    error_t err = ERROR_NONE;
    error_t out_err = ERROR_NONE;

    if (!settings_manager || !settings) {
        RCLOG_WARN("[settings manager] copy_settings_to_manager missing parameters: settings_manager=%p, settings=%p", settings_manager, settings);
        return ERROR_UNSPECIFIC;
    }

    // only copy valid passwords, indicate error if invalid but continue
    if ((copy_password == SETTINGS_COPY_PASSWORD) && !validate_password(settings->password)) {
        out_err = SETTINGS_MANAGER_ERROR_PASSWORD_INVALID;
        copy_password = SETTINGS_KEEP_PASSWORD;
    }

    err = lock_settings_manager(settings_manager);
    if (err != ERROR_NONE) {
        return err;
    }

    err = copy_settings(settings_manager->settings, settings, copy_password);
    if (err != ERROR_NONE) {
        out_err = err;
    }

    unlock_settings_manager(settings_manager);

    return out_err;
}

error_t copy_settings_from_manager(settings_manager_t *settings_manager, settings_t *settings, bool copy_password) {
    error_t err = ERROR_NONE;

    if (!settings_manager || !settings) {
        RCLOG_WARN("[settings manager] copy_settings_from_manager missing parameters: settings_manager=%p, settings=%p", settings_manager, settings);
        return ERROR_UNSPECIFIC;
    }

    err = lock_settings_manager(settings_manager);
    if (err != ERROR_NONE) {
        return err;
    }

    err = copy_settings(settings, settings_manager->settings, copy_password);

    unlock_settings_manager(settings_manager);

    return err;
}
