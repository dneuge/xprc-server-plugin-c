#ifndef XPRC_LICENSE_WINDOW_H
#define XPRC_LICENSE_WINDOW_H

#include "img_window.h"
#include "../licenses.h"

#include "../server_manager.h"
#include "../settings_manager.h"

typedef struct {
    /// ImGui window instance
    img_window window;

    // defaults
    xprc_license_t *default_license;

    // current state - set by program
    xprc_license_t *selected_license;
    float bottom_space_needed;

    // current state - set by user
    bool accepted;

    // requested state changes (will be reset once processed)
    xprc_license_t *select_license;
    bool should_save;
    bool should_disable;
    bool reset_scroll_position;

    list_t *licenses;

    // settings
    /// shared settings manager instance
    settings_manager_t *settings_manager;
    /// shared server manager instance
    server_manager_t *server_manager;
} license_window_t;

/**
 * Creates a new license window instance.
 * @param settings_manager shared settings manager instance to connect with
 * @param server_manager server manager to connect with
 * @return license window instance; NULL on error
 */
license_window_t* create_license_window(settings_manager_t *settings_manager, server_manager_t *server_manager);

/**
 * Destroy an license window instance.
 * @param license_window startup window to destroy
 */
void destroy_license_window(license_window_t* license_window);

/**
 * Opens the license window on screen.
 * @param license_window startup window to open
 */
void open_license_window(license_window_t* license_window);

#endif //XPRC_LICENSE_WINDOW_H