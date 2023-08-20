#include <stdio.h>

#include "utils.h"

#include "settings_manager.h"

settings_manager_t* create_settings_manager() {
    settings_manager_t *settings_manager = zalloc(sizeof(settings_manager_t));
    if (!settings_manager) {
        return NULL;
    }

    settings_manager->settings = create_settings();
    if (!settings_manager->settings) {
        goto error;
    }

    if (mtx_init(&settings_manager->mutex, mtx_plain | mtx_recursive) == thrd_success) {
        return settings_manager;
    }

    error:
    destroy_settings_manager(settings_manager);
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
        printf("[XPRC] failed to lock settings manager mutex during destruction; continuing regardless\n");
    } else {
        mtx_unlock(&settings_manager->mutex);
    }

    destroy_settings(settings_manager->settings);
    settings_manager->settings = NULL;

    mtx_destroy(&settings_manager->mutex);

    free(settings_manager);

    return ERROR_NONE;
}

error_t lock_settings_manager(settings_manager_t *settings_manager) {
    if (!settings_manager) {
        return ERROR_UNSPECIFIC;
    }

    if (settings_manager->destruction_pending) {
        return ERROR_DESTRUCTION_PENDING;
    }

    if (mtx_lock(&settings_manager->mutex) != thrd_success) {
        return ERROR_LOCK_FAILED;
    }

    if (!settings_manager->destruction_pending) {
        return ERROR_NONE;
    }

    unlock_settings_manager(settings_manager);
    return ERROR_DESTRUCTION_PENDING;
}

void unlock_settings_manager(settings_manager_t *settings_manager) {
    if (!settings_manager) {
        return;
    }

    mtx_unlock(&settings_manager->mutex);
}