#ifndef GUI_H
#define GUI_H

/**
 * @file gui.h XPRC GUI root
 */

#include <stdbool.h>

#include <XPLMMenus.h>

typedef struct _gui_t gui_t;

#include "../settings_manager.h"
#include "settings_window.h"

/// XPRC GUI root
typedef struct _gui_t {
    /// the window providing access to all XPRC settings
    settings_window_t *settings_window;

    /// X-Plane's plugin menu ID as presented to this plugin
    XPLMMenuID xp_plugins_menu_id;
    /// the index of this plugin's sub-menu in the X-Plane plugin menu
    int plugins_menu_subitem_index;
    /// the ID of XPRC's plugin sub-menu where further items can be appended to
    XPLMMenuID menu_id;
} gui_t;

/**
 * Creates a new GUI root instance.
 * @param settings_manager shared settings manager instance to connect with
 * @return GUI root instance; NULL on error
 */
gui_t* gui_create(settings_manager_t *settings_manager);

/**
 * Destroys the given GUI root instance.
 * @param gui GUI root instance to destroy
 */
void gui_destroy(gui_t *gui);

#endif
