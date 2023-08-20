#ifndef XPRC_SETTINGS_MANAGER_H
#define XPRC_SETTINGS_MANAGER_H

#include "settings.h"

/**
 * @file settings_manager.h central instance for managing XPRC settings
 */

/**
 * Container for #settings_t to be shared among different threads; use #lock_shared_settings() and
 * #unlock_shared_settings() around access to the contained settings instance.
 */
typedef struct {
    /// mutex used to coordinate access with
    mtx_t mutex;
    /// only clean-up is permitted if destruction is pending (true)
    bool destruction_pending;

    /// settings meant to be protected by the mutex; use #lock_shared_settings() and #unlock_shared_settings() to
    /// access this field
    settings_t *settings;
} shared_settings_t;

/**
 * Creates a new shared settings container in default configuration.
 * @return shared settings container in default configuration; NULL on error
 */
shared_settings_t* create_shared_settings();

/**
 * Destroys the given shared settings container including the settings instance inside.
 * @param shared_settings shared settings container to destroy
 * @return error code; #ERROR_NONE on success
 */
error_t destroy_shared_settings(shared_settings_t *shared_settings);

/**
 * Tries to lock the given shared settings container.
 * @param shared_settings shared settings container to lock
 * @return error code; #ERROR_NONE on success
 */
error_t lock_shared_settings(shared_settings_t *shared_settings);

/**
 * Unlocks the given shared settings container; must only be called if a lock is actually being held.
 * @param shared_settings shared settings container to unlock
 */
void unlock_shared_settings(shared_settings_t *shared_settings);


#endif //XPRC_SETTINGS_MANAGER_H
