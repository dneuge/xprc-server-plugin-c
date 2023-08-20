#include <stdio.h>

#include "utils.h"

#include "settings_manager.h"

shared_settings_t* create_shared_settings() {
    shared_settings_t *shared_settings = zalloc(sizeof(shared_settings_t));
    if (!shared_settings) {
        return NULL;
    }

    shared_settings->settings = create_settings();
    if (!shared_settings->settings) {
        goto error;
    }

    if (mtx_init(&shared_settings->mutex, mtx_plain | mtx_recursive) == thrd_success) {
        return shared_settings;
    }

    error:
    destroy_shared_settings(shared_settings);
    return NULL;
}

error_t destroy_shared_settings(shared_settings_t *shared_settings) {
    error_t err = ERROR_NONE;

    if (!shared_settings) {
        return ERROR_NONE;
    }

    err = lock_shared_settings(shared_settings);
    if (err != ERROR_NONE) {
        return err;
    }

    shared_settings->destruction_pending = true;
    unlock_shared_settings(shared_settings);

    // lock & unlock once more to make sure every thread had a chance to notice pending destruction
    if (mtx_lock(&shared_settings->mutex) != thrd_success) {
        printf("[XPRC] failed to lock shared settings mutex during destruction; continuing regardless\n");
    } else {
        mtx_unlock(&shared_settings->mutex);
    }

    destroy_settings(shared_settings->settings);
    shared_settings->settings = NULL;

    mtx_destroy(&shared_settings->mutex);

    free(shared_settings);

    return ERROR_NONE;
}

error_t lock_shared_settings(shared_settings_t *shared_settings) {
    if (!shared_settings) {
        return ERROR_UNSPECIFIC;
    }

    if (shared_settings->destruction_pending) {
        return ERROR_DESTRUCTION_PENDING;
    }

    if (mtx_lock(&shared_settings->mutex) != thrd_success) {
        return ERROR_LOCK_FAILED;
    }

    if (!shared_settings->destruction_pending) {
        return ERROR_NONE;
    }

    unlock_shared_settings(shared_settings);
    return ERROR_DESTRUCTION_PENDING;
}

void unlock_shared_settings(shared_settings_t *shared_settings) {
    if (!shared_settings) {
        return;
    }

    mtx_unlock(&shared_settings->mutex);
}