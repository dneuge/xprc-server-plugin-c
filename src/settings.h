#ifndef XPRC_SETTINGS_H
#define XPRC_SETTINGS_H

/**
 * @file settings.h XPRC settings including persistence
 */

#include <stdbool.h>
#include <stdlib.h>
#include <threads.h>

#include "errors.h"

/// password is copied when passed to #copy_settings()
#define SETTINGS_COPY_PASSWORD true
/// password is kept unmodified when passed to #copy_settings()
#define SETTINGS_KEEP_PASSWORD false

/**
 * XPRC settings accessible to users
 */
typedef struct {
    /// expected password/token to authenticate clients to the XPRC server when opening a new session
    char *password;
    /// automatically start XPRC? true enables, false disables autostart
    bool auto_startup;
    /// automatically generate a new password when starting XPRC? true enabled, false disables password regeneration
    bool auto_regen_password;
    /// hostname/IP of the network interface to bind XPRC to
    char *network_interface;
    /// TCP port number to bind XPRC to
    int network_port;
    /// true enables IPv6 support; false disables IPv6
    bool network_enable_ipv6;
} settings_t;

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

/**
 * Creates a new settings instance in default configuration.
 * @return settings instance in default configuration; NULL on error
 */
settings_t* create_settings();

/**
 * Destroys a settings instance, incl. strings.
 * @param settings instance to destroy
 */
void destroy_settings(settings_t *settings);

/**
 * Copies all settings from given source onto destination instance.
 * @param dest will be updated to match the source settings; strings will be reallocated (values are copied), existing strings will be freed
 * @param src source to read settings to copy from
 * @param copy_password copies the password as well if true; original password in destination remains unchanged if false; use defines SETTINGS_*_PASSWORD
 * @return error code; #ERROR_NONE on success
 */
error_t copy_settings(settings_t *dest, settings_t *src, bool copy_password);

/**
 * Loads all settings except the password from given file (password is persisted in a separate file).
 * @param dest will be updated with loaded settings; strings will be reallocated (values are copied), existing strings will be freed
 * @param filepath path to the settings file to load
 * @return error code; #ERROR_NONE on success
 */
error_t load_settings_without_password(settings_t *dest, char *filepath);

/**
 * Loads only the password from given file (persisted separate from other settings); the password will not be validated.
 * @param dest will be updated to loaded, unvalidated password; strings will be reallocated (values are copied), existing strings will be freed
 * @param filepath path to the password file to load
 * @return error code; #ERROR_NONE on success
 */
error_t load_password(settings_t *dest, char *filepath);

/**
 * Saves all settings except the password to the given file (password needs to be persisted to a separate file).
 * @param settings will be persisted to the file (except password)
 * @param filepath path to the settings file to save (will be overwritten if it exists)
 * @return error code; #ERROR_NONE on success
 */
error_t save_settings_without_password(settings_t *settings, char *filepath);

/**
 * Saves only the password to the given file (persisted separate from other settings).
 * @param settings password will be persisted to the file
 * @param filepath path to the password file to save (will be overwritten if it exists)
 * @return error code; #ERROR_NONE on success
 */
error_t save_password(settings_t *settings, char *filepath);

#endif //XPRC_SETTINGS_H
