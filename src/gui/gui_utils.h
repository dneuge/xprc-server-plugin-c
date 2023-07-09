#ifndef GUI_UTILS_H
#define GUI_UTILS_H

#include "img_window.h"

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui.h>

#define IM_VEC2(X,Y) (struct ImVec2){.x = X, .y = Y}
#define IM_VEC4(X,Y,Z,W) (struct ImVec4){.x = X, .y = Y, .z = Z, .w = W}

#define IMGUI_ZERO_SIZE IM_VEC2(0,0)

#define IM_RED IM_VEC4(1.0f, 0.0f, 0.0f, 1.0f)

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
