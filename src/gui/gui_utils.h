#ifndef GUI_UTILS_H
#define GUI_UTILS_H

/**
 * @file gui_utils.h helper functions for ImGui
 */

#include "img_window.h"

/// required to include cimgui.h from C
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui.h>

/**
 * Defines an ImVec2 value with given coordinates.
 * @param X x-coordinate for the ImVec2 structure
 * @param Y y-coordinate for the ImVec2 structure
 * @return an ImVec2 value with given coordinates
 */
#define IM_VEC2(X,Y) (struct ImVec2){.x = X, .y = Y}

/**
 * Defines an ImVec4 value with given coordinates.
 * @param X x-coordinate for the ImVec4 structure
 * @param Y y-coordinate for the ImVec4 structure
 * @param Z z-coordinate for the ImVec4 structure
 * @param W w-coordinate for the ImVec4 structure
 * @return an ImVec4 value with given coordinates
 */
#define IM_VEC4(X,Y,Z,W) (struct ImVec4){.x = X, .y = Y, .z = Z, .w = W}

/// 0 width and 0 height as an ImVec2 structure value
#define IMGUI_ZERO_SIZE IM_VEC2(0,0)

/// red color represented as an ImVec4 structure value
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

/**
 * Moves the imgui cursor by th given offset if not 0.
 *
 * @param x_offset X offset to apply
 * @param y_offset Y offset to apply
 */
void offset_imgui_cursor(float x_offset, float y_offset);

#endif
