#ifndef SETTINGS_WINDOW_H
#define SETTINGS_WINDOW_H

/**
 * @file settings_window.h XPRC settings window
 */

#include "img_window.h"

#include "../server_manager.h"
#include "../settings_manager.h"

#include "../lists.h"

/// everything related to the XPRC settings window
typedef struct {
    /// ImGui window instance
    img_window window;

    /// the window contains unapplied changes if marked dirty (true); if not (false) then the currently shown settings match the current configuration
    bool dirty;
    /// validation state; if true, all fields are valid to be applied; at least one field contains errors if false
    bool valid;

    // settings
    /// shared settings manager instance
    settings_manager_t *settings_manager;
    /// shared server manager instance
    server_manager_t *server_manager;
    /// locally editable settings instance
    settings_t *settings;

    // UI
    /// length of password in settings (needs to be updated whenever password changes)
    size_t password_length;
    /// shows the current password if true; password will be hidden if false
    bool reveal_password;

    /// "copy password" button state; pressed when true, released when false
    bool btn_pwd_copy_state;
    /// "reveal password" button state; pressed when true, released when false
    bool btn_pwd_reveal_state;
    /// "regenerate password" button state; pressed when true, released when false
    bool btn_pwd_regen_state;

    /// all network interface options to be shown on the GUI, as displayed
    list_t *network_interface_options;
    /// "reset network configuration" button state; pressed when true, released when false
    bool btn_network_reset_state;

    /// "apply changes" button state; pressed when true, released when false
    bool btn_apply_state;
    /// "discard changes" button state; pressed when true, released when false
    bool btn_discard_state;
} settings_window_t;

/**
 * Creates a new settings window instance.
 * @param settings_manager shared settings manager instance to connect with
 * @param server_manager server manager to connect with
 * @return settings window instance; NULL on error
 */
settings_window_t* create_settings_window(settings_manager_t *settings_manager, server_manager_t *server_manager);
/**
 * Destroy a settings window instance.
 * @param settings_window settings window to destroy
 */
void destroy_settings_window(settings_window_t* settings_window);

/**
 * Opens the settings window on screen.
 * @param settings_window settings window to open
 */
void open_settings_window(settings_window_t* settings_window);

#endif
