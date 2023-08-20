#ifndef XPRC_SETTINGS_MANAGER_H
#define XPRC_SETTINGS_MANAGER_H

#include "settings.h"

/**
 * @file settings_manager.h central instance for managing XPRC settings
 */

/**
 * Container for #settings_t to be shared among different threads; use #lock_settings_manager() and
 * #unlock_settings_manager() around access to the contained settings instance.
 */
typedef struct {
    /// mutex used to coordinate access with
    mtx_t mutex;
    /// only clean-up is permitted if destruction is pending (true)
    bool destruction_pending;

    /// settings meant to be protected by the mutex; use #lock_settings_manager() and #unlock_settings_manager() to
    /// access this field
    settings_t *settings;
} settings_manager_t;

/**
 * Creates a new settings manager starting in default configuration.
 * @return settings manager with default configuration; NULL on error
 */
settings_manager_t* create_settings_manager();

/**
 * Destroys the given settings manager including the settings instance inside.
 * @param settings_manager instance to destroy
 * @return error code; #ERROR_NONE on success
 */
error_t destroy_settings_manager(settings_manager_t *settings_manager);

/**
 * Tries to lock the given settings manager.
 * @param settings_manager instance to lock
 * @return error code; #ERROR_NONE on success
 */
error_t lock_settings_manager(settings_manager_t *settings_manager);

/**
 * Unlocks the given settings manager; must only be called if a lock is actually being held.
 * @param settings_manager instance to unlock
 */
void unlock_settings_manager(settings_manager_t *settings_manager);


#endif //XPRC_SETTINGS_MANAGER_H
