#ifndef XPRC_SETTINGS_MANAGER_H
#define XPRC_SETTINGS_MANAGER_H

#include "settings.h"

/// returned in case both user settings and password file did not exist which probably indicates a fresh installation,
/// see #configure_settings_manager_from_storage() for full details
#define SETTINGS_MANAGER_ERROR_NO_PREVIOUS_SETTINGS (SETTINGS_MANAGER_ERROR_BASE + 0)

/// returned in case user settings could not be restored and default settings are used instead,
/// see #configure_settings_manager_from_storage() for full details
#define SETTINGS_MANAGER_ERROR_SETTINGS_REVERTED (SETTINGS_MANAGER_ERROR_BASE + 1)

/// returned in case user settings could not be saved
#define SETTINGS_MANAGER_ERROR_SETTINGS_NOT_SAVED (SETTINGS_MANAGER_ERROR_BASE + 2)

/// returned in case the previous password could not be restored and a randomly generated one is used instead
#define SETTINGS_MANAGER_ERROR_PASSWORD_NOT_LOADED (SETTINGS_MANAGER_ERROR_BASE + 3)

/// returned in case the current (newly generated) password could not be stored, meaning clients need manual input
#define SETTINGS_MANAGER_ERROR_PASSWORD_NOT_SAVED (SETTINGS_MANAGER_ERROR_BASE + 4)

/// returned in case the previous password does not fulfill password policy (server will refuse to start)
#define SETTINGS_MANAGER_ERROR_PASSWORD_INVALID (SETTINGS_MANAGER_ERROR_BASE + 5)

/// returned in case the current port could not be stored, meaning clients need manual input
#define SETTINGS_MANAGER_ERROR_PORT_NOT_SAVED (SETTINGS_MANAGER_ERROR_BASE + 6)

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

    /// path to directory holding XPRC files (shared and server-specific directories)
    char *xprc_directory;
    /// path to password file
    char *password_filepath;
    /// path to port file
    char *port_filepath;

    /// path to directory holding server-specific files (e.g. settings file)
    char *server_directory;
    /// path to settings file
    char *settings_filepath;

    /// settings meant to be protected by the mutex; use #lock_settings_manager() and #unlock_settings_manager() to
    /// access this field
    settings_t *settings;
} settings_manager_t;

/**
 * Creates a new settings manager starting in default configuration.
 * @param xprc_directory path to standardized XPRC settings directory, without trailing separator (string will be copied; original to be managed by caller)
 * @param server_directory path to server-specific settings directory, without trailing separator (string will be copied; original to be managed by caller)
 * @return settings manager with default configuration; NULL on error
 */
settings_manager_t* create_settings_manager(char *xprc_directory, char *server_directory);

/**
 * Destroys the given settings manager including the settings instance inside.
 * @param settings_manager instance to destroy
 * @return error code; #ERROR_NONE on success
 */
error_t destroy_settings_manager(settings_manager_t *settings_manager);

/**
 * Tries to lock the given settings manager; called by other functions implicitly when needed but is exposed for when
 * consistency is required over multiple function calls.
 * @param settings_manager instance to lock
 * @return error code; #ERROR_NONE on success
 */
error_t lock_settings_manager(settings_manager_t *settings_manager);

/**
 * Unlocks the given settings manager; must only be called if a lock is actually being held.
 * @param settings_manager instance to unlock
 */
void unlock_settings_manager(settings_manager_t *settings_manager);

/**
 * Copies the given settings onto the instance held by the manager for coordination across components.
 *
 * Invalid passwords are rejected, indicating #SETTINGS_MANAGER_ERROR_PASSWORD_INVALID, but other settings will be
 * taken over.
 *
 * @param settings_manager manager instance
 * @param settings settings to be copied to manager
 * @param copy_password copies the password if true and valid; original password in manager settings remains unchanged if false or invalid; use defines #SETTINGS_COPY_PASSWORD and #SETTINGS_KEEP_PASSWORD
 * @return error code; #ERROR_NONE on success
 */
error_t copy_settings_to_manager(settings_manager_t *settings_manager, settings_t *settings, bool copy_password);

/**
 * Copies the current settings held by the manager onto the given settings instance.
 * @param settings_manager manager instance
 * @param settings will be updated to match settings held by the manager
 * @param copy_password copies the password if true; original password in manager settings remains unchanged if false; use defines #SETTINGS_COPY_PASSWORD and #SETTINGS_KEEP_PASSWORD
 * @return error code; #ERROR_NONE on success
 */
error_t copy_settings_from_manager(settings_manager_t *settings_manager, settings_t *settings, bool copy_password);

/**
 * Attempts to load previously persisted settings and also updates the password if requested, which will be written out
 * to storage as needed.
 *
 * Loaded settings may be incomplete and instead replaced by default values. An error indication means loading failed
 * at least partially but some settings (or the password) might actually have been restored. Error indication is mainly
 * useful to know when to ask users to check their settings or for debugging but will not prevent the plugin from
 * working.
 *
 * Specific error codes are as follows, in descending order of priority (higher priority errors hide lower ones):
 *
 * * #SETTINGS_MANAGER_ERROR_SETTINGS_REVERTED: In case user settings failed to load, they are reset to default values.
 *   Users should be made aware that the (safe) defaults have been restored and they should check their settings.
 *   The password will have been randomly generated if defaults apply.
 *
 * * #SETTINGS_MANAGER_ERROR_PASSWORD_INVALID: Returned if the (loaded) password is invalid. This will cause server
 *   startup to fail.
 *
 * * #SETTINGS_MANAGER_ERROR_PASSWORD_NOT_LOADED: Returned in case user settings were loaded and indicate the password
 *   should *not* be regenerated on startup but the password could not be loaded. The server will be able to start but
 *   use a randomly generated password instead which also has not been persisted to storage. User should be informed.
 *
 * * #SETTINGS_MANAGER_ERROR_SETTINGS_NOT_SAVED: Returned if settings were initially created (first startup detected)
 *   but failed to be saved.
 *
 * * #SETTINGS_MANAGER_ERROR_PASSWORD_NOT_SAVED: Returned if password was automatically generated but could not be
 *   saved.
 *
 * * #SETTINGS_MANAGER_ERROR_NO_PREVIOUS_SETTINGS: Upon first startup none of the files (neither user settings nor
 *   password file) will be present; this may also indicate a total failure of storage access but is more generally just
 *   a clean installation. Users should be shown a greeting popup which would also make them aware of any issue if it's
 *   actually not their first startup.
 *
 * @return error code, see full documentation for details on interpretation
 */
// TODO: also indicate an error if settings do not validate
error_t configure_settings_manager_from_storage(settings_manager_t *settings_manager);

/**
 * Writes the settings currently held by the manager to persistent storage, including the password.
 *
 * @param settings_manager manager whose settings should be persisted
 * @return error code; #ERROR_NONE on success
 */
error_t persist_settings_from_manager(settings_manager_t *settings_manager);

#endif //XPRC_SETTINGS_MANAGER_H
