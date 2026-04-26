#ifndef XPRC_ABOUT_WINDOW_H
#define XPRC_ABOUT_WINDOW_H

#include "../dependencies.h"
#include "img_window.h"
#include "../licenses.h"

#include "../server_manager.h"
#include "../settings_manager.h"

#define ABOUT_WINDOW_TAB_NO_CHANGE       (0)
#define ABOUT_WINDOW_TAB_COMPONENTS      (1)
#define ABOUT_WINDOW_TAB_LICENSES        (2)
#define ABOUT_WINDOW_TAB_ACKNOWLEDGMENTS (3)
#define ABOUT_WINDOW_TAB_FIRST           (ABOUT_WINDOW_TAB_COMPONENTS)

typedef struct {
    xprc_dependency_copyright_t *copyright;
    xprc_license_t *license; // may be null
    char *license_text_link; // may be null
} about_window_copyright_t;

typedef struct {
    xprc_dependency_t *dependency;
    list_t *copyrights;
} about_window_dependency_t;

typedef struct {
    /// ImGui window instance
    img_window window;

    // defaults
    xprc_license_t *default_license;

    // current state
    xprc_license_t *selected_license;
    bool all_licenses_accepted;
    float bottom_space_needed;

    // requested state changes (will be reset once processed)
    uint8_t select_tab;
    xprc_license_t *select_license;
    bool reset_scroll_position;
    bool reject_licenses;

    // more efficient data structures and cached values need to be prepared ahead of time
    list_t *licenses;
    list_t *dependencies;
    list_t *trademarks_acknowledgments;
    xprc_license_t *binary_license;
    xprc_license_t *source_license;
    char *xprc_binary_license_link;
    char *xprc_source_license_link;
    char *xprc_website_label;
    char *xprc_build_id;
    int plugin_copyright_year;

    // settings
    /// shared license manager instance
    license_manager_t *license_manager;
} about_window_t;

/**
 * Creates a new "about" window instance.
 * @param license_manager shared license manager to connect with
 * @return "about" window instance; NULL on error
 */
about_window_t* create_about_window(license_manager_t *license_manager);

/**
 * Destroy an "about" window instance.
 * @param about_window "about" window to destroy
 */
void destroy_about_window(about_window_t* about_window);

/**
 * Opens the "about" window on screen.
 * @param about_window "about" window to open
 */
void open_about_window(about_window_t* about_window);

#endif //XPRC_ABOUT_WINDOW_H