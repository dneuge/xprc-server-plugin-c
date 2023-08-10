#ifndef SETTINGS_WINDOW_H
#define SETTINGS_WINDOW_H

/**
 * @file settings_window.h XPRC settings window
 */

#include "img_window.h"

/// everything related to the XPRC settings window
typedef struct {
    /// ImGui window instance
    img_window window;

    /// the window contains unapplied changes if marked dirty (true); if not (false) then the currently shown settings match the current configuration
    bool dirty;
    /// validation state; if true, all fields are valid to be applied; at least one field contains errors if false
    bool valid;

    // settings
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

    // UI
    /// shows the current password if true; password will be hidden if false
    bool reveal_password;

    /// minimum allowed TCP port number
    int network_port_min;
    /// maximum allowed TCP port number (incl.)
    int network_port_max;

    /// "copy password" button state; pressed when true, released when false
    bool btn_pwd_copy_state;
    /// "reveal password" button state; pressed when true, released when false
    bool btn_pwd_reveal_state;
    /// "regenerate password" button state; pressed when true, released when false
    bool btn_pwd_regen_state;

    /// "reset network configuration" button state; pressed when true, released when false
    bool btn_network_reset_state;

    /// "apply changes" button state; pressed when true, released when false
    bool btn_apply_state;
    /// "discard changes" button state; pressed when true, released when false
    bool btn_discard_state;
} settings_window_t;

/**
 * Creates a new settings window instance.
 * @return settings window instance; NULL on error
 */
settings_window_t* create_settings_window();
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
