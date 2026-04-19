#include <XPLMDisplay.h>

#include "gui_utils.h"

typedef struct {
    int left;
    int top;
    int right;
    int bottom;
} gui_bounds_t;

static gui_bounds_t get_xplane_global_screen_bounds(){
    gui_bounds_t bounds = {0,};
    XPLMGetScreenBoundsGlobal(&bounds.left, &bounds.top, &bounds.right, &bounds.bottom);
    return bounds;
}

/**
 * Calculates the window bounds as needed to create a window in X-Plane.
 * @param xplane_screen_bounds global screen bounds as reported by X-Plane
 * @param offset_x offset from left border
 * @param offset_y offset from top border
 * @param width window width
 * @param height window height
 * @return X-Plane window bounds
 */
static gui_bounds_t calculate_xplane_window_bounds(gui_bounds_t *xplane_screen_bounds, int offset_x, int offset_y, int width, int height) {
    gui_bounds_t out = {0,};

    out.left = xplane_screen_bounds->left + offset_x;
    out.top = xplane_screen_bounds->top - offset_y;
    out.right = out.left + width;
    out.bottom = out.top - height;

    return out;
}

/**
 * Calculates the window bounds as needed to create a centered window in X-Plane.
 * @param xplane_screen_bounds global screen bounds as reported by X-Plane
 * @param width window width
 * @param height window height
 * @return X-Plane window bounds
 */
static gui_bounds_t calculate_xplane_centered_window_bounds(gui_bounds_t *xplane_screen_bounds, int width, int height) {
    int half_screen_width = (xplane_screen_bounds->right - xplane_screen_bounds->left) / 2;
    int half_screen_height = (xplane_screen_bounds->top - xplane_screen_bounds->bottom) / 2;
    int half_width = width / 2;
    int half_height = height / 2;

    return calculate_xplane_window_bounds(
        xplane_screen_bounds,
        half_screen_width - half_width,
        half_screen_height - half_height,
        width,
        height
    );
}

img_window create_centered_window(int width, int height, img_window_build_interface_f build_interface, img_window_on_show_f on_show, void *ref) {
    gui_bounds_t global_screen_bounds = get_xplane_global_screen_bounds();
    gui_bounds_t window_bounds = calculate_xplane_centered_window_bounds(&global_screen_bounds, width, height);
    return img_window_create(window_bounds.left, window_bounds.top, window_bounds.right, window_bounds.bottom,
                             xplm_WindowDecorationRoundRectangle, xplm_WindowLayerFloatingWindows,
                             build_interface, on_show, ref);
}

void offset_imgui_cursor(const float x_offset, const float y_offset) {
    if (x_offset != 0.0f) {
        igSetCursorPosX(igGetCursorPosX() + x_offset);
    }

    if (y_offset != 0.0f) {
        igSetCursorPosY(igGetCursorPosY() + y_offset);
    }
}