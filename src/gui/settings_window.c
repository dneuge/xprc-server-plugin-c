#include <stdlib.h>

#include "gui_utils.h"
#include "../utils.h"

#include "settings_window.h"

static int imgui_test = 0;
static void imgui_update(img_window window, void *ref) {
    settings_window_t *settings_window = ref;

    igText("XPRC Test");
    igText("Count: %d", imgui_test);
    if (igButton("+", IMGUI_ZERO_SIZE)) {
        imgui_test++;
    }
    if (igButton("-", IMGUI_ZERO_SIZE)) {
        imgui_test--;
    }
}

settings_window_t* create_settings_window() {
    settings_window_t *settings_window = zalloc(sizeof(settings_window_t));
    if (!settings_window) {
        return NULL;
    }

    settings_window->window = create_centered_window(400, 300,imgui_update, NULL, settings_window);
    if (!settings_window->window) {
        printf("[XPRC] failed to create settings window\n");
        free(settings_window);
        return NULL;
    }

    img_window_set_title(settings_window->window, "XPRC Settings");

    img_window_set_visible(settings_window->window, true); // DEBUG

    return settings_window;
}

void destroy_settings_window(settings_window_t* settings_window) {
    if (!settings_window) {
        return;
    }

    img_window_destroy(settings_window->window);
    free(settings_window);
}
