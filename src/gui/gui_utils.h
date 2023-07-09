#ifndef GUI_UTILS_H
#define GUI_UTILS_H

#include "img_window.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui.h>

#define IMGUI_ZERO_SIZE (struct ImVec2){.x = 0, .y = 0}

/**
 * Creates an img_window centered on screen.
 *
 * @param width window width
 * @param height window height
 * @param build_interface will be called each frame for window contents and event handling
 * @param on_show optional override to suppress opening windows; set to null if not needed
 * @param ref reference pointer provided to callbacks
 * @return img_window instance
 */
img_window create_centered_window(int width, int height, img_window_build_interface_f build_interface, img_window_on_show_f on_show, void *ref);

#endif
