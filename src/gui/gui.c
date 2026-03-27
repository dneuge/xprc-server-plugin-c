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

static void toggle_server_state(gui_t *gui) {
    error_t err = ERROR_NONE;

    bool is_running = is_running_server_state(get_managed_server_state(gui->server_manager));
    RCLOG_DEBUG("[GUI] toggle server state from %d", is_running);
    if (is_running) {
        err = stop_managed_server(gui->server_manager);
    } else {
        err = start_managed_server(gui->server_manager);
    }

    if (err != ERROR_NONE) {
        RCLOG_WARN("[GUI] failed to toggle server state from %d: %d", is_running, err);
    }
}

static void xp_menu_callback(void *inMenuRef, void *inItemRef) {
    gui_t *gui = inMenuRef;
    menu_item_t menu_item = (menu_item_t) inItemRef;

    if (!gui) {
        RCLOG_ERROR("menu handler called without GUI menu reference");
        return;
    }

    switch (menu_item) {
        case MENU_ITEM_TOGGLE:
            RCLOG_DEBUG("menu handler called for toggle");
            toggle_server_state(gui);
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

static void on_managed_server_state_change(void *context, managed_server_state_t new_state) {
    if (!context) {
        return;
    }

    gui_t *gui = context;

    gui->actual_managed_server_state = new_state;
}

static void update_menu(gui_t *gui) {
    if (!gui) {
        return;
    }

    // accessed without mutex - copy value for consistency within this cycle
    managed_server_state_t new_state = gui->actual_managed_server_state;
    if (new_state == gui->processed_managed_server_state) {
        // no change, nothing to do
        return;
    }

    bool was_running = is_running_server_state(gui->processed_managed_server_state);
    bool is_running = is_running_server_state(new_state);
    RCLOG_TRACE("[GUI] update_menu %d/%d => %d/%d", gui->processed_managed_server_state, was_running, new_state, is_running);
    gui->processed_managed_server_state = new_state;

    // update menu item only if state actually toggled between running and stopped
    if (is_running != was_running) {
        XPLMCheckMenuItem(
            gui->menu_id,
            gui->start_stop_subitem_index,
            is_running ? xplm_Menu_Checked : xplm_Menu_Unchecked
        );
    }
}

gui_t* gui_create(settings_manager_t *settings_manager, server_manager_t *server_manager) {
    // called inside XP context

    gui_t *gui = zmalloc(sizeof(gui_t));
    if (!gui) {
        return NULL;
    }

    gui->server_manager = server_manager;
    gui->plugins_menu_subitem_index = -1;

    gui->processed_managed_server_state = SERVER_STATE_UNKNOWN;
    gui->actual_managed_server_state = SERVER_STATE_UNKNOWN;

    img_window_init_globals();

    gui->settings_window = create_settings_window(settings_manager, server_manager);
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

    gui->start_stop_subitem_index = XPLMAppendMenuItem(gui->menu_id, "Start/stop server", (void*) MENU_ITEM_TOGGLE, 0);
    if (gui->start_stop_subitem_index < 0) {
        RCLOG_ERROR("appending toggle menu item failed: %d", gui->start_stop_subitem_index);
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

    // start/stop menu item needs to be updated to reflect intended server state
    on_managed_server_state_change(gui, get_managed_server_state(server_manager));
    register_server_state_listener(server_manager, on_managed_server_state_change, gui);
    update_menu(gui);

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

    error_t err = unregister_server_state_listener(gui->server_manager, on_managed_server_state_change, gui);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[GUI] failed to unregister server state listener: %d", err);
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

void maintain_gui(gui_t *gui) {
    update_menu(gui);
}
