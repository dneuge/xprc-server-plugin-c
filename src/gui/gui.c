#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "img_window.h"
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
        printf("[XPRC] menu handler called without GUI menu reference\n");
        return;
    }

    switch (menu_item) {
        case MENU_ITEM_TOGGLE:
            printf("[XPRC] menu handler called for toggle\n"); // FIXME: implement
            break;

        case MENU_ITEM_SETTINGS:
            printf("[XPRC] menu handler called for settings\n");
            open_settings_window(gui->settings_window);
            break;

        case MENU_ITEM_ABOUT:
            printf("[XPRC] menu handler called for about\n"); // FIXME: implement
            break;

        default:
            printf("[XPRC] menu handler called for unhandled item %lu\n", menu_item);
            return;
    }
}

gui_t* gui_create() {
    // called inside XP context

    gui_t *gui = zalloc(sizeof(gui_t));
    if (!gui) {
        return NULL;
    }

    gui->plugins_menu_subitem_index = -1;

    img_window_init_globals();

    gui->settings_window = create_settings_window();
    if (!gui->settings_window) {
        goto error;
    }

    gui->xp_plugins_menu_id = XPLMFindPluginsMenu();
    if (!gui->xp_plugins_menu_id) {
        printf("[XPRC] plugins menu not found\n");
        goto error;
    }

    gui->plugins_menu_subitem_index = XPLMAppendMenuItem(gui->xp_plugins_menu_id, PLUGIN_MENU_XPRC_NAME, NULL, 0);
    if (gui->plugins_menu_subitem_index < 0) {
        printf("[XPRC] appending to plugin menu failed: %d\n", gui->plugins_menu_subitem_index);
        goto error;
    }

    gui->menu_id = XPLMCreateMenu(PLUGIN_MENU_XPRC_NAME, gui->xp_plugins_menu_id, gui->plugins_menu_subitem_index, xp_menu_callback, gui);
    if (!gui->menu_id) {
        printf("[XPRC] handler registration to plugin menu failed\n");
        goto error;
    }

    int xprc_subitem_index;

    xprc_subitem_index = XPLMAppendMenuItem(gui->menu_id, "Start/stop server", (void*) MENU_ITEM_TOGGLE, 0);
    if (xprc_subitem_index < 0) {
        printf("[XPRC] appending toggle menu item failed: %d\n", xprc_subitem_index);
        goto error;
    }

    xprc_subitem_index = XPLMAppendMenuItem(gui->menu_id, "Settings", (void*) MENU_ITEM_SETTINGS, 0);
    if (xprc_subitem_index < 0) {
        printf("[XPRC] appending settings menu item failed: %d\n", xprc_subitem_index);
        goto error;
    }

    xprc_subitem_index = XPLMAppendMenuItem(gui->menu_id, "About", (void*) MENU_ITEM_ABOUT, 0);
    if (xprc_subitem_index < 0) {
        printf("[XPRC] appending about menu item failed: %d\n", xprc_subitem_index);
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
