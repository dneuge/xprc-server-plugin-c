#ifndef XPRC_SETTINGS_H
#define XPRC_SETTINGS_H

/**
 * @file settings.h XPRC settings including persistence
 */

#include <stdbool.h>
#include <stdlib.h>

#include "threads_compat.h"

#include "errors.h"

/// password is copied when passed to #copy_settings()
#define SETTINGS_COPY_PASSWORD true
/// password is kept unmodified when passed to #copy_settings()
#define SETTINGS_KEEP_PASSWORD false

#define XPRC_DEFAULT_NETWORK_INTERFACE INTERFACE_LOCAL

// log levels are redefined for settings/persistence to be independent of log system changes
#define SETTINGS_LOG_LEVEL_ERROR (20)
#define SETTINGS_LOG_LEVEL_WARN (40)
#define SETTINGS_LOG_LEVEL_INFO (60)
#define SETTINGS_LOG_LEVEL_DEBUG (80)
#define SETTINGS_LOG_LEVEL_TRACE (100)

#define SETTINGS_DEFAULT_LOG_LEVEL_CONSOLE SETTINGS_LOG_LEVEL_INFO
#define SETTINGS_DEFAULT_LOG_LEVEL_XPLANE SETTINGS_LOG_LEVEL_INFO

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
    /// log level to use for X-Plane
    int log_level_xplane;
    /// log level to use for console output
    int log_level_console;
} settings_t;

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
 * @param copy_password copies the password as well if true; original password in destination remains unchanged if false; use defines #SETTINGS_COPY_PASSWORD and #SETTINGS_KEEP_PASSWORD
 * @return error code; #ERROR_NONE on success
 */
error_t copy_settings(settings_t *dest, settings_t *src, bool copy_password);

/**
 * Checks if the given settings are valid.
 * @param settings settings to check
 * @param check_password if false, invalid passwords will not fail validation; otherwise password validation is included
 * @return true if valid, false if not
 */
bool validate_settings(settings_t *settings, bool check_password);

/**
 * Constrains values back into valid ranges.
 *
 * Note that the only constrainable numeric value at this moment is the network port, other settings will remain
 * unaffected.
 * @param settings settings to constrain
 * @return true if settings had to be constrained, false if handled settings were already in range
 */
bool constrain_settings(settings_t *settings);

/**
 * Resets only network-relevant settings to default values.
 * @param settings settings to reset network part of
 * @return error code; #ERROR_NONE on success
 */
error_t reset_network_settings(settings_t *settings);

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

/**
 * Applies log levels from settings to log system.
 * @param settings settings to apply
 */
void configure_logger_from_settings(settings_t *settings);

#endif //XPRC_SETTINGS_H
