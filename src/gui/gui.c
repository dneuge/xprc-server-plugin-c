#include <stdlib.h>

#include "img_window.h"
#include "../utils.h"

#include "gui.h"

gui_t* gui_create() {
    // called inside XP context

    gui_t *gui = zalloc(sizeof(gui_t));
    if (!gui) {
        return NULL;
    }

    img_window_init_globals();

    gui->settings_window = create_settings_window();
    if (!gui->settings_window) {
        gui_destroy(gui);
        return NULL;
    }

    return gui;
}


void gui_destroy(gui_t *gui) {
    // called inside XP context

    if (!gui) {
        return;
    }

    destroy_settings_window(gui->settings_window);

    free(gui);

    img_window_destroy_globals();
}
