#ifndef GUI_H
#define GUI_H

/**
 * @file gui.h XPRC GUI root
 */

#include <XPLMMenus.h>

typedef struct _gui_t gui_t;

#include "about_window.h"
#include "license_window.h"
#include "settings_window.h"

/// XPRC GUI root
typedef struct _gui_t {
    /// the window providing access to all XPRC settings
    settings_window_t *settings_window;

    /// the window detailing license and dependency information
    about_window_t *about_window;

    /// the "modal" window requiring user to accept new licenses
    license_window_t *license_window;

    /// shared manager controlling server instances
    server_manager_t *server_manager;

    /// X-Plane's plugin menu ID as presented to this plugin
    XPLMMenuID xp_plugins_menu_id;
    /// the index of this plugin's sub-menu in the X-Plane plugin menu
    int plugins_menu_subitem_index;
    /// the ID of XPRC's plugin sub-menu where further items can be appended to
    XPLMMenuID menu_id;

    /// index of the "Start/Stop Server" menu item
    int start_stop_subitem_index;

    // NOTE: updated without a mutex from different threads - this *should* be safe as it will still lead to eventual
    //       consistency as the value is processed repeatedly and is only used for displaying the last seen state
    /// last state that was applied to GUI (i.e. currently shown to user)
    managed_server_state_t processed_managed_server_state;
    /// latest state to apply asynchronously; if another state is set before this one could be processed, the later one
    /// will invalidate the older state, meaning there is not guarantee for a state change to get processed to GUI
    managed_server_state_t actual_managed_server_state;
} gui_t;

/**
 * Creates a new GUI root instance.
 *
 * GUI is only half-functional as license acceptance may still be pending, #gui_complete_init must be called to fully
 * enable the plugin.
 *
 * @param license_manager shared license manager to connect with
 * @param settings_manager shared settings manager instance to connect with
 * @param server_manager shared server manager instance to connect with
 * @return GUI root instance; NULL on error
 */
gui_t* gui_create(license_manager_t *license_manager, settings_manager_t *settings_manager, server_manager_t *server_manager);

/**
 * Completes GUI initialization; only to be called after licenses have been accepted by user.
 *
 * @param gui GUI root instance to complete initialization for
 * @return error code; #ERROR_NONE on success
 */
error_t gui_complete_init(gui_t *gui);

/**
 * Destroys the given GUI root instance.
 * @param gui GUI root instance to destroy
 */
void gui_destroy(gui_t *gui);

/**
 * Triggered within X-Plane context at regular intervals to perform maintenance.
 * @param gui GUI root instance to maintain
 */
void maintain_gui(gui_t *gui);

#endif
