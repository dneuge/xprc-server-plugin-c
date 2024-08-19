#include <stdint.h>

#include "img_window.h"
#include "../logger.h"
#include "../utils.h"

#include "gui.h"

#define PLUGIN_MENU_XPRC_NAME "XPRC"

#define MENU_ITEM_TOGGLE 1
#define MENU_ITEM_SETTINGS 2
#define MENU_ITEM_ABOUT 3
typedef uint64_t menu_item_t;

static void xp_menu_callback(void *inMenuRef, void *inItemRef) {
    gui_t *gui = inMenuRef;
    menu_item_t menu_item = (menu_item_t) inItemRef;

    if (!gui) {
        RCLOG_ERROR("menu handler called without GUI menu reference");
        return;
    }

    switch (menu_item) {
        case MENU_ITEM_TOGGLE:
            RCLOG_DEBUG("menu handler called for toggle"); // FIXME: implement
            break;

        case MENU_ITEM_SETTINGS:
            RCLOG_DEBUG("menu handler called for settings");
            open_settings_window(gui->settings_window);
            break;

        case MENU_ITEM_ABOUT:
            RCLOG_DEBUG("menu handler called for about"); // FIXME: implement
            break;

        default:
            RCLOG_WARN("menu handler called for unhandled item %lu", menu_item);
            return;
    }
}

gui_t* gui_create(settings_manager_t *settings_manager) {
    // called inside XP context

    gui_t *gui = zalloc(sizeof(gui_t));
    if (!gui) {
        return NULL;
    }

    gui->plugins_menu_subitem_index = -1;

    img_window_init_globals();

    gui->settings_window = create_settings_window(settings_manager);
    if (!gui->settings_window) {
        goto error;
    }

    gui->xp_plugins_menu_id = XPLMFindPluginsMenu();
    if (!gui->xp_plugins_menu_id) {
        RCLOG_ERROR("plugins menu not found");
        goto error;
    }

    gui->plugins_menu_subitem_index = XPLMAppendMenuItem(gui->xp_plugins_menu_id, PLUGIN_MENU_XPRC_NAME, NULL, 0);
    if (gui->plugins_menu_subitem_index < 0) {
        RCLOG_ERROR("appending to plugin menu failed: %d", gui->plugins_menu_subitem_index);
        goto error;
    }

    gui->menu_id = XPLMCreateMenu(PLUGIN_MENU_XPRC_NAME, gui->xp_plugins_menu_id, gui->plugins_menu_subitem_index, xp_menu_callback, gui);
    if (!gui->menu_id) {
        RCLOG_ERROR("handler registration to plugin menu failed");
        goto error;
    }

    int xprc_subitem_index;

    xprc_subitem_index = XPLMAppendMenuItem(gui->menu_id, "Start/stop server", (void*) MENU_ITEM_TOGGLE, 0);
    if (xprc_subitem_index < 0) {
        RCLOG_ERROR("appending toggle menu item failed: %d", xprc_subitem_index);
        goto error;
    }

    xprc_subitem_index = XPLMAppendMenuItem(gui->menu_id, "Settings", (void*) MENU_ITEM_SETTINGS, 0);
    if (xprc_subitem_index < 0) {
        RCLOG_ERROR("appending settings menu item failed: %d", xprc_subitem_index);
        goto error;
    }

    xprc_subitem_index = XPLMAppendMenuItem(gui->menu_id, "About", (void*) MENU_ITEM_ABOUT, 0);
    if (xprc_subitem_index < 0) {
        RCLOG_ERROR("appending about menu item failed: %d", xprc_subitem_index);
        goto error;
    }

    return gui;

error:
    gui_destroy(gui);
    return NULL;
}


void gui_destroy(gui_t *gui) {
    // called inside XP context

    if (!gui) {
        return;
    }

    if (gui->menu_id) {
        XPLMClearAllMenuItems(gui->menu_id);
        XPLMDestroyMenu(gui->menu_id);
        gui->menu_id = NULL;
    }

    if (gui->xp_plugins_menu_id && gui->plugins_menu_subitem_index >= 0) {
        XPLMRemoveMenuItem(gui->xp_plugins_menu_id, gui->plugins_menu_subitem_index);
        gui->plugins_menu_subitem_index = -1;
    }

    gui->xp_plugins_menu_id = NULL;

    destroy_settings_window(gui->settings_window);

    free(gui);

    img_window_destroy_globals();
}
