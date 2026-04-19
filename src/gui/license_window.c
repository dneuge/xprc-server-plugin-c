#include "gui_utils.h"
#include "../logger.h"
#include "../utils.h"

#include "license_window.h"

// TODO: relocate to build info?
#define XPRC_BINARY_LICENSE_ID "_xprc-binary"

#define DEFAULT_LICENSE_ID XPRC_BINARY_LICENSE_ID

// in case imgui is a shared instance (may be introduced to XP12) we select "random" or prefixed IDs
// to hopefully use our own namespace
#define IMGUI_ID_PREFIX "xprc_c_enq_license_"
#define IMGUI_ID_BASE (332452)

#define IMGUI_ID_LICENSE_SCROLLPANE (IMGUI_ID_BASE + 0)

#define LICENSE_WINDOW_WIDTH 1020
#define LICENSE_WINDOW_HEIGHT 750

#define LIST_WIDTH (200.0f)
#define LICENSE_PANE_SPACING (10.0f)
#define BUTTON_SPACING (10.0f)
#define CHECKBOX_LABEL_OFFSET (24.0f)
#define SMALL_VERTICAL_SPACING (3.0f)
#define SPACE_ADJUSTMENT_TOLERANCE (0.05f)

// FIXME: inhibit server start while not accepted
// FIXME: check & show automatically on startup if needed
// TODO: indicate to user which licenses changed/have been added since last accepted? (and the reason why the dialog is shown again)
// TODO: add button to about dialog to decline later?

static void update_view(license_window_t *license_window) {
    // === top area ===

    igTextWrapped(
        "XPRC requires all of the following licenses to be accepted before it can be used:"
    );
    igText("");

    // === middle area ===

    xprc_license_t *selected_license = license_window->selected_license;

    ImVec2 available_space = IM_VEC2(0, 0);
    igGetContentRegionAvail(&available_space);

    float middle_space_available = available_space.y - license_window->bottom_space_needed;
    if (middle_space_available < 0.0f) {
        middle_space_available = 1.0f;
    }

    if (igBeginListBox("##" IMGUI_ID_PREFIX "list", IM_VEC2(LIST_WIDTH, middle_space_available))) {
        for (list_item_t *item = license_window->licenses->head; item; item = item->next) {
            xprc_license_t *license = item->value;
            const bool selected = (license == selected_license);

            if (igSelectable_Bool(license->name, selected, ImGuiSelectableFlags_None, IM_VEC2(0, 0))) {
                RCLOG_DEBUG("[license window] user selected license in list: %s", license->id);
                license_window->select_license = license;
            }

            if (selected) {
                igSetItemDefaultFocus();
            }
        }

        igEndListBox();
    }

    igSameLine(0, LICENSE_PANE_SPACING);
    igGetContentRegionAvail(&available_space);

    if (available_space.x > 0) {
        ImGuiID id_license_scrollpane = igGetID_Int(IMGUI_ID_LICENSE_SCROLLPANE);
        bool license_scrollpane_visible = igBeginChild_ID(id_license_scrollpane, IM_VEC2(available_space.x, middle_space_available), ImGuiChildFlags_None, ImGuiWindowFlags_None);
        if (license_scrollpane_visible) {
            if (license_window->reset_scroll_position) {
                igSetScrollY_Float(0.0f);
                license_window->reset_scroll_position = false;
            }

            igTextWrapped("%s", selected_license->text);
        }
        igEndChild();
    }

    // === bottom area ===

    offset_imgui_cursor(LIST_WIDTH + LICENSE_PANE_SPACING, SMALL_VERTICAL_SPACING);
    igCheckbox("I have read and accept ALL licenses and associated disclaimers##" IMGUI_ID_PREFIX "accepted", &license_window->accepted);

    offset_imgui_cursor(LIST_WIDTH + LICENSE_PANE_SPACING + CHECKBOX_LABEL_OFFSET, SMALL_VERTICAL_SPACING);
    igBeginDisabled(license_window->accepted);
    if (igButton("Decline & disable XPRC##" IMGUI_ID_PREFIX "btn_decline", IM_VEC2(0, 0))) {
        RCLOG_ERROR("[license window] user declines licenses and wants to disable XPRC");
        license_window->should_disable = true;
    }
    igEndDisabled();
    igSameLine(0, BUTTON_SPACING);
    igBeginDisabled(!license_window->accepted);
    if (igButton("Save & enable XPRC##" IMGUI_ID_PREFIX "btn_accept", IM_VEC2(0, 0))) {
        RCLOG_INFO("[license window] user accepts licenses and wants to enable XPRC");
        license_window->should_save = true;
    }
    igEndDisabled();

    // adjust the space we have to reserve to fully render the area below the license display for next iteration
    igGetContentRegionAvail(&available_space);
    if (available_space.y > SPACE_ADJUSTMENT_TOLERANCE || available_space.y < -SPACE_ADJUSTMENT_TOLERANCE) {
        // FIXME: limit to specific range?
        RCLOG_DEBUG("[license window] bottom area reserved y=%.2f which yields y=%.2f size difference, adjusting", license_window->bottom_space_needed, available_space.y);
        license_window->bottom_space_needed -= available_space.y;
    }
}

static void handle_view_state(license_window_t *license_window) {
    if (!license_window) {
        return;
    }

    if (license_window->select_license) {
        if (license_window->selected_license != license_window->select_license) {
            license_window->reset_scroll_position = true;
        }

        license_window->selected_license = license_window->select_license;
        license_window->select_license = NULL;
    }

    if (license_window->should_disable) {
        // FIXME: XPRC should not be initialized
        igCloseCurrentPopup();

        license_window->should_disable = false;
    } else if (license_window->should_save && license_window->accepted) {
        // FIXME: update & store settings
        // FIXME: XPRC should continue to initialize
        igCloseCurrentPopup();

        license_window->should_save = false;
    }
}

static void imgui_update(img_window window, void *ref) {
    license_window_t *license_window = ref;

    update_view(license_window);
    handle_view_state(license_window);
}

static bool imgui_show(img_window window, void *ref) {
    // called by ImGui only if the window is not visible already

    license_window_t *license_window = ref;
    if (!license_window) {
        return false;
    }

    license_window->bottom_space_needed = 0.0f;
    license_window->reset_scroll_position = true;

    if (!license_window->select_license) {
        license_window->selected_license = license_window->default_license;
    } else {
        license_window->selected_license = license_window->select_license;
        license_window->select_license = NULL;
    }

    return true;
}

license_window_t* create_license_window(settings_manager_t *settings_manager, server_manager_t *server_manager) {
    if (!settings_manager || !server_manager) {
        RCLOG_ERROR("[license window] missing parameters to initialize; settings_manager=%p, server_manager=%p", settings_manager, server_manager);
        return NULL;
    }

    license_window_t *license_window = zmalloc(sizeof(license_window_t));
    if (!license_window) {
        return NULL;
    }

    license_window->settings_manager = settings_manager;
    license_window->server_manager = server_manager;

    license_window->default_license = xprc_get_license(DEFAULT_LICENSE_ID);
    if (!license_window->default_license) {
        RCLOG_ERROR("[license window] failed to get default license %s", DEFAULT_LICENSE_ID);
        goto error;
    }

    license_window->licenses = create_list();
    if (!license_window->licenses) {
        RCLOG_ERROR("[license window] failed to create license list");
        goto error;
    }
    list_t *license_ids = xprc_get_license_ids();
    if (!license_ids) {
        RCLOG_ERROR("[license window] failed to get license IDs");
        goto error;
    }
    for (list_item_t *item = license_ids->head; item; item = item->next) {
        char *license_id = item->value;
        xprc_license_t *license = xprc_get_license(license_id);
        if (!license) {
            RCLOG_ERROR("[license window] failed to get license %s", license_id);
            destroy_list(license_ids, NULL);
            goto error;
        }

        if (!list_append(license_window->licenses, license)) {
            RCLOG_ERROR("[license window] failed to record license %s in list", license_id);
            destroy_list(license_ids, NULL);
            goto error;
        }
    }
    destroy_list(license_ids, NULL);

    license_window->window = create_centered_window(LICENSE_WINDOW_WIDTH, LICENSE_WINDOW_HEIGHT,imgui_update, imgui_show, license_window);
    if (!license_window->window) {
        RCLOG_WARN("[license window] failed to create imgui window");
        goto error;
    }

    img_window_set_title(license_window->window, "XPRC Licenses");

    return license_window;

error:
    destroy_license_window(license_window);
    return NULL;
}

void destroy_license_window(license_window_t* license_window) {
    if (!license_window) {
        return;
    }

    img_window_destroy(license_window->window);

    license_window->select_license = NULL;
    license_window->selected_license = NULL;
    license_window->default_license = NULL;

    if (license_window->licenses) {
        destroy_list(license_window->licenses, NULL);
        license_window->licenses = NULL;
    }

    license_window->settings_manager = NULL;
    license_window->server_manager = NULL;

    free(license_window);
}

void open_license_window(license_window_t* license_window) {
    if (!license_window || !license_window->window) {
        return;
    }

    img_window_set_visible(license_window->window, true);
}