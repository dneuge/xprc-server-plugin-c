#include <string.h>

#include "../_buildinfo.h"
#include "../dependencies.h"
#include "gui_utils.h"
#include "../licenses.h"
#include "../logger.h"
#include "../utils.h"

#include "about_window.h"

#include "../trademarks.h"

// TODO: relocate to build info?
#define XPRC_BINARY_LICENSE_ID "_xprc-binary"
#define XPRC_SOURCE_LICENSE_ID "MIT"

#define MIN_PLUGIN_COPYRIGHT_YEAR (2026)
#define MAX_PLUGIN_COPYRIGHT_YEAR (2099)

#define DEFAULT_LICENSE_ID XPRC_BINARY_LICENSE_ID

// in case imgui is a shared instance (may be introduced to XP12) we select "random" or prefixed IDs
// to hopefully use our own namespace
#define IMGUI_ID_PREFIX "xprc_c_enq_about_"
#define IMGUI_ID_BASE (439168)
#define IMGUI_ID_DEPENDENCY_LIST (IMGUI_ID_BASE + 1)
#define IMGUI_ID_LICENSE_SCROLLPANE (IMGUI_ID_BASE + 2)
#define IMGUI_ID_TRADEMARK_ACKNOWLEDGMENT_SCROLLPANE (IMGUI_ID_BASE + 3)

#define ABOUT_WINDOW_WIDTH 1045
#define ABOUT_WINDOW_HEIGHT 700

#define BANNER_LINE_1 "#   # ####  ####   ####"
#define BANNER_LINE_2 " # #  #   # #   # #    "
#define BANNER_LINE_3 "  #   ####  ####  #    "
#define BANNER_LINE_4 " # #  #     #  #  #    "
#define BANNER_LINE_5 "#   # #     #   #  ####"

static const ImGuiSeparatorFlags separator_flags_horizontal = ImGuiSeparatorFlags_Horizontal;
static const float separator_dependency_thickness = 2.0f;
static const float separator_copyright_thickness = 1.0f;
static const float separator_trademark_acknowledgment_thickness = 1.0f;
static const float separator_windows_section_thickness = 1.0f;

const ImVec4 color_text_inactive = IM_VEC4(0.5f, 0.5f, 0.5f, 1.0f);

static void render_dependency(about_window_dependency_t *window_dependency, float available_width, xprc_license_t **select_license) {
    if (!select_license || !window_dependency) {
        return;
    }

    xprc_dependency_t *dependency = window_dependency->dependency;

    if (!dependency->active) {
        igPushStyleColor_Vec4(ImGuiCol_Text, color_text_inactive);
    }

    igSeparatorTextEx(0, dependency->name, NULL, separator_dependency_thickness);

    if (dependency->version) {
        igText("version ");
        igSameLine(0, 0);
        igText("%s", dependency->version);
    }

    if (dependency->url) {
        igTextLinkOpenURL(dependency->url, dependency->url);
    }

    if (dependency->excerpt) {
        igText("excerpt: ");
        igSameLine(0, 0);
        igText("%s", dependency->excerpt);
    }

    if (dependency->activation) {
        igText("%s", dependency->activation);
        igSameLine(0, 0);
        igText(dependency->active ? " [currently active]" : " [currently inactive]");
    }

    for (list_item_t *item = window_dependency->copyrights->head; item; item = item->next) {
        about_window_copyright_t *window_copyright = item->value;

        xprc_dependency_copyright_t *copyright = window_copyright->copyright;
        xprc_license_t *license = window_copyright->license;

        igSeparatorEx(separator_flags_horizontal, separator_copyright_thickness);
        if (license && window_copyright->license_text_link) {
            igText("licensed ");
            igSameLine(0, 0);
            if (igTextLink(window_copyright->license_text_link)) {
                RCLOG_DEBUG("[about window] clicked copyright license link: %s", license->id);
                *select_license = license;
            }
        }

        igTextWrapped("%s", copyright->copyright_remark);
    }

    igText("");

    if (!dependency->active) {
        igPopStyleColor(1);
    }
}

static void render_tab_content_components(about_window_t *about_window) {
    igText("%s     %s %s", BANNER_LINE_1, XPRC_SERVER_NAME, XPRC_SERVER_VERSION);

    igText("%s     Copyright (c) 2022-%d Daniel Neugebauer", BANNER_LINE_2, about_window->plugin_copyright_year);

    igText("%s     ", BANNER_LINE_3);
    if (about_window->xprc_website_label) {
        igSameLine(0, 0);
        igTextLinkOpenURL(about_window->xprc_website_label, XPRC_SERVER_WEBSITE);
    }

    igText("%s", BANNER_LINE_4);

    igText("%s     released under ", BANNER_LINE_5);
    igSameLine(0, 0);
    if (igTextLink(about_window->xprc_binary_license_link)) {
        about_window->select_license = about_window->binary_license;
    }
    igSameLine(0, 0);
    igText(", developed under ");
    igSameLine(0, 0);
    if (igTextLink(about_window->xprc_source_license_link)) {
        about_window->select_license = about_window->source_license;
    }

    igText("");
    if (about_window->xprc_build_id) {
        igText("               Build ID     %s", about_window->xprc_build_id);
    } else {
        igText("               Build ID     (not set)");
    }
    igText("             Build time     %s", XPRC_SERVER_BUILD_TIME);
    igText("           Build target     %s", XPRC_SERVER_BUILD_TARGET);
    igText("       Source reference     %s", XPRC_SERVER_BUILD_REF);

    igText("");
    igText("Included dependencies, in alphabetical order:");

    ImVec2 available_space = IM_VEC2(0, 0);
    igGetContentRegionAvail(&available_space);

    ImGuiID id_dependency_list = igGetID_Int(IMGUI_ID_DEPENDENCY_LIST);
    bool dependency_list_visible = igBeginChild_ID(id_dependency_list, IM_VEC2(available_space.x, available_space.y), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    if (dependency_list_visible) {
        ImVec2 available_inner_space = IM_VEC2(0, 0);
        igGetContentRegionAvail(&available_inner_space);

        for (list_item_t *item = about_window->dependencies->head; item; item = item->next) {
            about_window_dependency_t *dependency = item->value;
            render_dependency(dependency, available_inner_space.x, &about_window->select_license);
        }
    }
    igEndChild();
}

static void render_tab_content_licenses(about_window_t *about_window) {
    if (!about_window || !about_window->selected_license) {
        return;
    }

    xprc_license_t *selected_license = about_window->selected_license;

    igText("License: ");
    igSameLine(0, 0);

    const char *selected_license_name = selected_license->name;
    if (igBeginCombo("##" IMGUI_ID_PREFIX "licenses_combo", selected_license_name, ImGuiComboFlags_None)) {
        for (list_item_t *item = about_window->licenses->head; item; item = item->next) {
            xprc_license_t *license = item->value;
            const bool selected = (license == selected_license);

            if (igSelectable_Bool(license->name, selected, ImGuiSelectableFlags_None, IM_VEC2(0, 0))) {
                RCLOG_DEBUG("[about window] user selected license in drop-down: %s", license->id);
                about_window->select_license = license;
            }

            if (selected) {
                igSetItemDefaultFocus();
            }
        }
        igEndCombo();
    }

    igSeparatorEx(ImGuiSeparatorFlags_Horizontal, separator_windows_section_thickness);

    ImVec2 available_space = IM_VEC2(0, 0);
    igGetContentRegionAvail(&available_space);

    ImGuiID id_license_scrollpane = igGetID_Int(IMGUI_ID_LICENSE_SCROLLPANE);
    bool license_scrollpane_visible = igBeginChild_ID(id_license_scrollpane, IM_VEC2(available_space.x, available_space.y), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    if (license_scrollpane_visible) {
        igTextWrapped("%s", selected_license->text);
    }
    igEndChild();
}

static void render_tab_content_acknowledgments(about_window_t *about_window) {
    if (!about_window || !about_window->trademarks_acknowledgments) {
        return;
    }

    igTextWrapped(
        "The following trademark notices and acknowledgments relate to XPRC or references made by XPRC, "
        "its dependencies or accompanying documentation (incl. this dialog), not implying any affiliation "
        "with this project: (listed in alphabetical order)"
    );
    igText("");

    ImVec2 available_space = IM_VEC2(0, 0);
    igGetContentRegionAvail(&available_space);

    ImGuiID id_scrollpane = igGetID_Int(IMGUI_ID_TRADEMARK_ACKNOWLEDGMENT_SCROLLPANE);
    bool scrollpane_visible = igBeginChild_ID(id_scrollpane, IM_VEC2(available_space.x, available_space.y), ImGuiChildFlags_None, ImGuiWindowFlags_None);
    if (scrollpane_visible) {
        bool first = true;
        for (list_item_t *item = about_window->trademarks_acknowledgments->head; item; item = item->next) {
            if (first) {
                first = false;
            } else {
                igSeparatorEx(ImGuiSeparatorFlags_Horizontal, separator_trademark_acknowledgment_thickness);
            }

            igTextWrapped("%s", item->value);
        }
    }
    igEndChild();
}

static void update_view(about_window_t *about_window) {
    if (igBeginTabBar(IMGUI_ID_PREFIX "_tabbar", ImGuiTabBarFlags_None)) {
        ImGuiTabItemFlags components_tab_flags = ImGuiTabItemFlags_None;
        ImGuiTabItemFlags licenses_tab_flags = ImGuiTabItemFlags_None;
        ImGuiTabItemFlags acknowledgments_tab_flags = ImGuiTabItemFlags_None;
        if (about_window->select_tab != ABOUT_WINDOW_TAB_NO_CHANGE) {
            if (about_window->select_tab == ABOUT_WINDOW_TAB_COMPONENTS) {
                components_tab_flags = components_tab_flags | ImGuiTabItemFlags_SetSelected;
            } else if (about_window->select_tab == ABOUT_WINDOW_TAB_LICENSES) {
                licenses_tab_flags = licenses_tab_flags | ImGuiTabItemFlags_SetSelected;
            } else if (about_window->select_tab == ABOUT_WINDOW_TAB_ACKNOWLEDGMENTS) {
                acknowledgments_tab_flags = acknowledgments_tab_flags | ImGuiTabItemFlags_SetSelected;
            }
            about_window->select_tab = ABOUT_WINDOW_TAB_NO_CHANGE;
        }

        if (igBeginTabItem("Components##" IMGUI_ID_PREFIX "tab_components", NULL, components_tab_flags)) {
            render_tab_content_components(about_window);
            igEndTabItem();
        }

        if (igBeginTabItem("Licenses##" IMGUI_ID_PREFIX "tab_licenses", NULL, licenses_tab_flags)) {
            render_tab_content_licenses(about_window);
            igEndTabItem();
        }

        if (igBeginTabItem("Acknowledgments/Trademarks##" IMGUI_ID_PREFIX "tab_acknowledgments", NULL, acknowledgments_tab_flags)) {
            render_tab_content_acknowledgments(about_window);
            igEndTabItem();
        }

        igEndTabBar();
    }
}

static void handle_view_state(about_window_t *about_window) {
    if (!about_window) {
        return;
    }

    if (about_window->select_license) {
        about_window->select_tab = ABOUT_WINDOW_TAB_LICENSES;
        about_window->selected_license = about_window->select_license;
        about_window->select_license = NULL;
    }
}

static void imgui_update(img_window window, void *ref) {
    about_window_t *about_window = ref;

    update_view(about_window);
    handle_view_state(about_window);
}

static bool imgui_show(img_window window, void *ref) {
    // called by ImGui only if the window is not visible already

    about_window_t *about_window = ref;
    if (!about_window) {
        return false;
    }

    if (!about_window->select_license) {
        about_window->selected_license = about_window->default_license;
    } else {
        about_window->selected_license = about_window->select_license;
        about_window->select_tab = ABOUT_WINDOW_TAB_LICENSES;
        about_window->select_license = NULL;
    }

    // start on first tab unless requested otherwise
    if (about_window->select_tab == ABOUT_WINDOW_TAB_NO_CHANGE) {
        about_window->select_tab = ABOUT_WINDOW_TAB_FIRST;
    }

    /*
    // window was not on screen before; take that chance to update/reset
    // to actual current settings (discarding any previous changes)
    RCLOG_DEBUG("about window will be shown, reading settings");
    copy_from_settings(about_window);
    */

    return true;
}

static about_window_copyright_t* create_about_window_copyright(xprc_dependency_copyright_t *copyright, char *dependency_id, unsigned int item_index) {
    if (!copyright || !dependency_id) {
        RCLOG_ERROR("[about window] create_about_window_copyright called with copyright=%p, dependency_id=%p", copyright, dependency_id);
        return NULL;
    }

    about_window_copyright_t *out = zmalloc(sizeof(about_window_copyright_t));
    if (!out) {
        return NULL;
    }

    out->copyright = copyright;

    char *license_id = copyright->license_id;
    if (license_id) {
        out->license = xprc_get_license(license_id);
        if (!out->license) {
            RCLOG_ERROR("[about window] failed to retrieve license %s", license_id);
            goto error;
        }

        out->license_text_link = dynamic_sprintf(
            "%s##__copyright_link__%s__%u",
            out->license->name, dependency_id, item_index
        );
        if (!out->license_text_link) {
            RCLOG_ERROR("[about window] failed to format link string");
            goto error;
        }
    }

    return out;

error:
    if (out->license_text_link) {
        free(out->license_text_link);
    }

    free(out);
    return NULL;
}

static void destroy_about_window_copyright(void *ref) {
    if (!ref) {
        return;
    }

    about_window_copyright_t *copyright = ref;

    copyright->copyright = NULL;
    copyright->license = NULL;

    if (copyright->license_text_link) {
        free(copyright->license_text_link);
        copyright->license_text_link = NULL;
    }

    free(ref);
}

static about_window_dependency_t* create_about_window_dependency(xprc_dependency_t *dependency) {
    list_t *original_copyrights = NULL;

    if (!dependency) {
        RCLOG_ERROR("[about window] create_about_window_dependency called with null");
        return NULL;
    }

    about_window_dependency_t *out = zmalloc(sizeof(about_window_dependency_t));
    if (!out) {
        return NULL;
    }

    out->dependency = dependency;

    out->copyrights = create_list();
    if (!out->copyrights) {
        RCLOG_ERROR("[about window] failed to create list for dependency copyrights");
        goto error;
    }

    original_copyrights = xprc_get_dependency_copyrights(dependency);
    if (!original_copyrights) {
        RCLOG_ERROR("[about window] failed to retrieve copyrights for %s", dependency->id);
        goto error;
    }
    unsigned int item_index = 0;
    for (list_item_t *item = original_copyrights->head; item; item = item->next, item_index++) {
        xprc_dependency_copyright_t *original_copyright = item->value;

        about_window_copyright_t *window_copyright = create_about_window_copyright(original_copyright, dependency->id, item_index);
        if (!window_copyright) {
            RCLOG_ERROR("[about window] failed to create copyright for %s", dependency->id);
            goto error;
        }

        if (!list_append(out->copyrights, window_copyright)) {
            RCLOG_ERROR("[about window] failed to record copyright for %s", dependency->id);
            destroy_about_window_copyright(window_copyright);
            goto error;
        }
    }

    goto end;

error:
    if (out->copyrights) {
        destroy_list(out->copyrights, destroy_about_window_copyright);
    }

    free(out);
    out = NULL;

end:
    if (original_copyrights) {
        destroy_list(original_copyrights, NULL);
    }

    return out;
}

static void destroy_about_window_dependency(void *ref) {
    if (!ref) {
        return;
    }

    about_window_dependency_t *dependency = ref;
    dependency->dependency = NULL;

    destroy_list(dependency->copyrights, destroy_about_window_copyright);
    dependency->copyrights = NULL;

    free(dependency);
}

about_window_t* create_about_window(settings_manager_t *settings_manager, server_manager_t *server_manager) {
    if (!settings_manager || !server_manager) {
        RCLOG_ERROR("[about window] missing parameters to initialize; settings_manager=%p, server_manager=%p", settings_manager, server_manager);
        return NULL;
    }

    about_window_t *about_window = zmalloc(sizeof(about_window_t));
    if (!about_window) {
        return NULL;
    }

    about_window->settings_manager = settings_manager;
    about_window->server_manager = server_manager;

    if (strcmp("", XPRC_SERVER_WEBSITE) != 0) {
        about_window->xprc_website_label = dynamic_sprintf("%s##%s_component_website_link", XPRC_SERVER_WEBSITE, IMGUI_ID_PREFIX);
        if (!about_window->xprc_website_label) {
            RCLOG_ERROR("[about window] failed to format website label");
            goto error;
        }
    }

    if (strcmp("", XPRC_SERVER_BUILD_ID) != 0) {
        about_window->xprc_build_id = copy_string(XPRC_SERVER_BUILD_ID);
        if (!about_window->xprc_build_id) {
            RCLOG_ERROR("[about window] failed to prepare build ID");
            goto error;
        }
    }

    xprc_date_t build_date = parse_iso_date(XPRC_SERVER_BUILD_TIME);
    if (!is_valid_date(&build_date)) {
        RCLOG_ERROR("[about window] failed to parse build date %s", XPRC_SERVER_BUILD_TIME);
        goto error;
    }
    about_window->plugin_copyright_year = build_date.year;
    if (about_window->plugin_copyright_year < MIN_PLUGIN_COPYRIGHT_YEAR || about_window->plugin_copyright_year > MAX_PLUGIN_COPYRIGHT_YEAR) {
        RCLOG_ERROR("[about window] unexpected copyright year %u", about_window->plugin_copyright_year);
        goto error;
    }

    about_window->trademarks_acknowledgments = xprc_get_trademarks_acknowledgments();
    if (!about_window->trademarks_acknowledgments) {
        RCLOG_ERROR("[about window] failed to retrieve trademark/acknowledgments list");
        goto error;
    }

    about_window->licenses = create_list();
    if (!about_window->licenses) {
        RCLOG_ERROR("[about window] failed to create license list");
        goto error;
    }

    about_window->binary_license = xprc_get_license(XPRC_BINARY_LICENSE_ID);
    if (!about_window->binary_license) {
        RCLOG_ERROR("[about window] failed to get binary license %s", XPRC_BINARY_LICENSE_ID);
        goto error;
    }

    about_window->xprc_binary_license_link = dynamic_sprintf("%s##%sxprc_binary_license", about_window->binary_license->name, IMGUI_ID_PREFIX);
    if (!about_window->xprc_binary_license_link) {
        RCLOG_ERROR("[about window] failed to format binary license link");
        goto error;
    }

    about_window->source_license = xprc_get_license(XPRC_SOURCE_LICENSE_ID);
    if (!about_window->source_license) {
        RCLOG_ERROR("[about window] failed to get source license %s", XPRC_SOURCE_LICENSE_ID);
        goto error;
    }

    about_window->xprc_source_license_link = dynamic_sprintf("%s##%sxprc_source_license", about_window->source_license->name, IMGUI_ID_PREFIX);
    if (!about_window->xprc_source_license_link) {
        RCLOG_ERROR("[about window] failed to format source license link");
        goto error;
    }

    about_window->default_license = xprc_get_license(DEFAULT_LICENSE_ID);
    if (!about_window->default_license) {
        RCLOG_ERROR("[about window] failed to get default license %s", DEFAULT_LICENSE_ID);
        goto error;
    }

    list_t *license_ids = xprc_get_license_ids();
    if (!license_ids) {
        RCLOG_ERROR("[about window] failed to get license IDs");
        goto error;
    }
    for (list_item_t *item = license_ids->head; item; item = item->next) {
        char *license_id = item->value;
        xprc_license_t *license = xprc_get_license(license_id);
        if (!license) {
            RCLOG_ERROR("[about window] failed to get license %s", license_id);
            destroy_list(license_ids, NULL);
            goto error;
        }

        if (!list_append(about_window->licenses, license)) {
            RCLOG_ERROR("[about window] failed to record license %s in list", license_id);
            destroy_list(license_ids, NULL);
            goto error;
        }
    }
    destroy_list(license_ids, NULL);

    about_window->dependencies = create_list();
    if (!about_window->dependencies) {
        RCLOG_ERROR("[about window] failed to create dependency list");
        goto error;
    }

    list_t *original_dependencies = xprc_get_dependencies();
    if (!original_dependencies) {
        goto error;
    }
    for (list_item_t *item = original_dependencies->head; item; item = item->next) {
        xprc_dependency_t *original_dependency = item->value;

        about_window_dependency_t *window_dependency = create_about_window_dependency(original_dependency);
        if (!window_dependency) {
            RCLOG_ERROR("[about window] failed to create window dependency for %s", original_dependency->id);
            destroy_list(original_dependencies, NULL);
            goto error;
        }
        if (!list_append(about_window->dependencies, window_dependency)) {
            RCLOG_ERROR("[about window] failed to record window dependency for %s", original_dependency->id);
            destroy_about_window_dependency(window_dependency);
            destroy_list(original_dependencies, NULL);
            goto error;
        }
    }
    destroy_list(original_dependencies, NULL);

    about_window->window = create_centered_window(ABOUT_WINDOW_WIDTH, ABOUT_WINDOW_HEIGHT,imgui_update, imgui_show, about_window);
    if (!about_window->window) {
        RCLOG_WARN("[about window] failed to create imgui window");
        goto error;
    }

    img_window_set_title(about_window->window, "About XPRC");

    return about_window;

error:
    destroy_about_window(about_window);
    return NULL;
}

void destroy_about_window(about_window_t* about_window) {
    if (!about_window) {
        return;
    }

    if (about_window->xprc_website_label) {
        free(about_window->xprc_website_label);
        about_window->xprc_website_label = NULL;
    }

    if (about_window->xprc_build_id) {
        free(about_window->xprc_build_id);
        about_window->xprc_build_id = NULL;
    }

    if (about_window->xprc_binary_license_link) {
        free(about_window->xprc_binary_license_link);
        about_window->xprc_binary_license_link = NULL;
    }

    if (about_window->xprc_source_license_link) {
        free(about_window->xprc_source_license_link);
        about_window->xprc_source_license_link = NULL;
    }

    if (about_window->licenses) {
        destroy_list(about_window->licenses, NULL);
        about_window->licenses = NULL;
    }

    if (about_window->dependencies) {
        destroy_list(about_window->dependencies, destroy_about_window_dependency);
        about_window->dependencies = NULL;
    }

    if (about_window->trademarks_acknowledgments) {
        destroy_list(about_window->trademarks_acknowledgments, NULL);
        about_window->trademarks_acknowledgments = NULL;
    }

    about_window->binary_license = NULL;
    about_window->source_license = NULL;

    about_window->default_license = NULL;

    about_window->selected_license = NULL;

    about_window->select_tab = ABOUT_WINDOW_TAB_NO_CHANGE;
    about_window->selected_license = NULL;

    about_window->settings_manager = NULL;
    about_window->server_manager = NULL;

    img_window_destroy(about_window->window);

    free(about_window);
}

void open_about_window(about_window_t* about_window) {
    if (!about_window || !about_window->window) {
        return;
    }

    img_window_set_visible(about_window->window, true);
}