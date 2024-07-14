#include <stdlib.h>
#include <string.h>

#include "gui_utils.h"
#include "../network.h"
#include "../utils.h"

#include "settings_window.h"

#define NETWORK_INTERFACE_LABEL_LOCAL "<local machine only> (safe default)"
#define NETWORK_INTERFACE_LABEL_ALL "<public on all interfaces> (caution, unsafe!)"

static void validate(settings_window_t *settings_window) {
    settings_window->valid = validate_settings(settings_window->settings, true);
}

static int cmp_network_interface_addresses(list_item_t *item_a, list_item_t *item_b) {
    return cmp_ip_address(item_a->value, item_b->value);
}

static bool interface_address_matches_default_option(void *value, void *ref) {
    bool is_localhost = !cmp_ipv4_address((char*) value, "127.0.0.1") || !cmp_ipv6_address((char*) value, "::1");
    return is_localhost;
}

/**
 * Checks if two interface options are logically equal.
 * @param a first interface; may be NULL
 * @param b second interface; may be NULL
 * @return true if both interfaces are logically equal (including both being NULL), false if not
 */
static bool interface_equals(char *a, char *b) {
    return cmp_ip_address(a, b) == 0;
}

/**
 * Refreshes the list of network interfaces from the operating system and updates related settings. In case the
 * previously selected network interface has become unavailable, the default interface will be selected instead.
 * @param settings_window
 * @return true if previous network interface became unavailable and selection changed to default, false if stable
 */
static bool update_network_interfaces(settings_window_t *settings_window) {
    settings_t *settings = settings_window->settings;
    char *tmp_copy = NULL;

    // get all available specific addresses
    list_t *interfaces_addresses = get_network_interfaces(settings->network_enable_ipv6);
    if (!interfaces_addresses) {
        printf("[XPRC] failed to copy retrieve network interfaces\n");
        return false;
    }

    // we will add our default options using placeholders in front of all specific addresses,
    // remove anything that would effectively duplicate those entries
    list_delete_items_where(interfaces_addresses, interface_address_matches_default_option, NULL, free);

    // sort for better readability
    list_t *interface_options = copy_list_sorted(interfaces_addresses, cmp_network_interface_addresses);

    // we no longer need the original list; destroy the structure but keep the values (shared with the sorted one)
    destroy_list(interfaces_addresses, NULL);
    interfaces_addresses = NULL;

    // prepend default options (in reverse order)
    if (!list_prepend(interface_options, NULL)) { // NULL = any (listen on all interfaces)
        goto error;
    }

    tmp_copy = copy_string(INTERFACE_LOCAL);
    if (!tmp_copy || !list_prepend(interface_options, tmp_copy)) {
        goto error;
    }

    tmp_copy = NULL;

    // swap with previous list on settings window
    list_t *previous_interface_options = settings_window->network_interface_options;
    settings_window->network_interface_options = interface_options;

    // destroy old list and values
    if (previous_interface_options) {
        destroy_list(previous_interface_options, free);
    }
    previous_interface_options = NULL;

    // check if previously selected interface is still valid
    if (list_find_test(interface_options, (list_value_test_f*) interface_equals, settings->network_interface)) {
        // interface is still valid, nothing changed
        return false;
    }

    // interface is gone, select default instead
    char *replaced_interface = settings->network_interface;
    settings->network_interface = copy_string(XPRC_DEFAULT_NETWORK_INTERFACE);
    if (XPRC_DEFAULT_NETWORK_INTERFACE && !settings->network_interface) {
        // copy failed; retain original option
        return false;
    }

    if (replaced_interface) {
        free(replaced_interface);
    }

    return true;

error:
    if (interface_options) {
        destroy_list(interface_options, free);
    }

    if (tmp_copy) {
        free(tmp_copy);
    }

    // because we failed to update we should indicate that the previous option remains active
    return false;
}

static void copy_from_settings(settings_window_t *settings_window) {
    printf("[XPRC] copy_from_settings\n"); // DEBUG

    error_t err = copy_settings_from_manager(settings_window->settings_manager, settings_window->settings, SETTINGS_COPY_PASSWORD);
    if (err != ERROR_NONE) {
        printf("[XPRC] failed to copy settings from manager to window: %d\n", err);
        settings_window->dirty = true;
        return;
    }

    settings_window->dirty = constrain_settings(settings_window->settings);
    settings_window->password_length = strlen(settings_window->settings->password);

    settings_window->dirty |= update_network_interfaces(settings_window);

    validate(settings_window);
}

static void apply_settings(settings_window_t *settings_window) {
    // check again if settings are valid, refuse to apply if not
    validate(settings_window);
    if (!settings_window->valid) {
        return;
    }

    // FIXME: save settings
    // FIXME: trigger server restart if it was running

    // copy actual server settings in case they might be different from what we just set up
    copy_from_settings(settings_window);
}

static int imgui_input_set_dirty(ImGuiInputTextCallbackData* data) {
    settings_window_t *settings_window = data->UserData;
    settings_window->dirty = true;
    return 1;
}

/**
 * Returns a shared string representing the given interface option for display to users on GUI.
 * @param interface interface option to represent
 * @return shared string representing the given interface for GUI; must not be freed, may be same as input
 */
static const char* interface_option_label(char *interface) {
    if (!interface) {
        return NETWORK_INTERFACE_LABEL_ALL;
    } else if (!strcmp(interface, INTERFACE_LOCAL)) {
        return NETWORK_INTERFACE_LABEL_LOCAL;
    } else {
        return interface;
    }
}

static void update_view(settings_window_t *settings_window) {
    settings_t *settings = settings_window->settings;

    float label_width = 80.0f;
    float input_offset_x = 8.0f;
    float button_spacing = 2.0f;

    settings_window->dirty |= igCheckbox("start XPRC automatically", &settings->auto_startup);

    igText(" ");

    igText("Password:");
    igSameLine(label_width + input_offset_x, 0.0f);
    ImGuiInputTextFlags password_text_flags = settings_window->reveal_password ? ImGuiInputTextFlags_ReadOnly
                                                                               : ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_Password;
    igInputText("##password", settings->password, settings_window->password_length, password_text_flags, NULL, NULL);
    igIndent(label_width);
    settings_window->btn_pwd_copy_state = igButton("Copy", IMGUI_ZERO_SIZE);
    igSameLine(0.0f, button_spacing);
    char *btn_reveal_text = settings_window->reveal_password ? "Hide" : "Reveal";
    settings_window->btn_pwd_reveal_state = igButton(btn_reveal_text, IM_VEC2(80, 0));
    igSameLine(0.0f, button_spacing);
    settings_window->btn_pwd_regen_state = igButton("Regenerate", IMGUI_ZERO_SIZE);
    settings_window->dirty |= igCheckbox("automatically regenerate on each start", &settings->auto_regen_password);
    igTextWrapped("Passwords are stored in clear text and supposed to be randomly generated. For security reasons, you can only read/copy and regenerate passwords but not customize it through this dialog.");
    igIndent(-label_width);

    igText(" ");

    const float indent_section = 25.0f;
    if (igCollapsingHeader_TreeNodeFlags("Network Options", ImGuiTreeNodeFlags_CollapsingHeader)) {
        igIndent(indent_section);

        igTextWrapped(
                "WARNING: For security reasons, XPRC should never be exposed to untrusted machines such as via the Internet. "
                "If in doubt, restrict the server only to localhost and disable IPv6. If you are unsure, click below to "
                "reset the network configuration to default values."
        );

        igText(" ");

        igText("Interface:");
        igSameLine(label_width + indent_section + input_offset_x, 0.0f);
        if (igBeginCombo("##interface", interface_option_label(settings->network_interface), 0)) {
            // drop-down is open
            list_item_t *item = settings_window->network_interface_options->head;
            while (item) {
                // TODO: interface_equals is expensive to call due to full IP address comparison; this should not be done every frame!
                bool is_selected = interface_equals(item->value, settings->network_interface);
                if (igSelectable_Bool(interface_option_label(item->value), is_selected, 0, IMGUI_ZERO_SIZE)) {
                    if (!is_selected) {
                        // selection changed
                        char *tmp = copy_string(item->value);
                        bool copy_successful = (!item->value || tmp);
                        if (copy_successful) {
                            if (settings->network_interface) {
                                free(settings->network_interface);
                            }
                            settings->network_interface = tmp;

                            is_selected = true;
                            settings_window->dirty = true;
                        }
                    }
                }

                if (is_selected) {
                    igSetItemDefaultFocus();
                }

                item = item->next;
            }

            igEndCombo();
        }

        igText("Port:");
        igSameLine(label_width + indent_section + input_offset_x, 0.0f);
        bool network_port_changed = igInputInt("##port", &settings->network_port, 1, 10,ImGuiInputTextFlags_CharsDecimal);
        if (network_port_changed) {
            constrain_settings(settings);
            settings_window->dirty = true;
        }

        bool ipv6_changed = igCheckbox("listen on IPv6", &settings->network_enable_ipv6);
        if (ipv6_changed) {
            update_network_interfaces(settings_window);
            settings_window->dirty = true;
        }

        igText(" ");

        settings_window->btn_network_reset_state = igButton("Reset network options", IMGUI_ZERO_SIZE);

        igIndent(-indent_section);
    }

    igText(" ");

    validate(settings_window); // must be checked before reaching the apply button

    bool server_started = true; // FIXME: get from actual server
    char *btn_apply_text = server_started ? "Apply & Restart" : "Apply";

    igTextColored(
            IM_RED,
            settings_window->valid ? " "
                                   : "Invalid settings detected."
    );
    igBeginDisabled(!(settings_window->dirty && settings_window->valid));
    settings_window->btn_apply_state = igButton(btn_apply_text, IMGUI_ZERO_SIZE);
    igEndDisabled();

    igSameLine(0.0f, button_spacing);
    igBeginDisabled(!settings_window->dirty);
    settings_window->btn_discard_state = igButton("Discard", IMGUI_ZERO_SIZE);
    igEndDisabled();
}

static void handle_view_state(settings_window_t *settings_window) {
    if (settings_window->btn_pwd_reveal_state) {
        settings_window->reveal_password = !settings_window->reveal_password;
    }

    if (settings_window->btn_pwd_regen_state) {
        // FIXME: regenerate password
        settings_window->dirty = true;
    }

    if (settings_window->btn_network_reset_state) {
        error_t err = reset_network_settings(settings_window->settings);
        if (err != ERROR_NONE) {
            printf("[XPRC] failed to reset network settings to default\n");
        } else {
            settings_window->dirty = true;
        }
    }

    if (settings_window->btn_discard_state) {
        copy_from_settings(settings_window);
    } else if (settings_window->btn_apply_state) {
        apply_settings(settings_window);
    }
}

static void imgui_update(img_window window, void *ref) {
    settings_window_t *settings_window = ref;

    update_view(settings_window);
    handle_view_state(settings_window);
}


static bool imgui_show(img_window window, void *ref) {
    // called by ImGui only if the window is not visible already

    settings_window_t *settings_window = ref;
    if (!settings_window) {
        return false;
    }

    // window was not on screen before; take that chance to update/reset
    // to actual current settings (discarding any previous changes)
    printf("[XPRC] settings window will be shown, reading settings\n");
    copy_from_settings(settings_window);

    return true;
}

settings_window_t* create_settings_window(settings_manager_t *settings_manager) {
    settings_window_t *settings_window = zalloc(sizeof(settings_window_t));
    if (!settings_window) {
        return NULL;
    }

    settings_window->settings_manager = settings_manager;

    settings_window->settings = create_settings();
    if (!settings_window->settings) {
        printf("[XPRC] failed to create settings for settings window\n");
        goto error;
    }

    // copy settings initially before we start defining the UI, just in case (will be updated again before opening the
    // window)
    copy_from_settings(settings_window);

    settings_window->window = create_centered_window(655, 485,imgui_update, imgui_show, settings_window);
    if (!settings_window->window) {
        printf("[XPRC] failed to create settings window\n");
        goto error;
    }

    img_window_set_title(settings_window->window, "XPRC Settings");

    return settings_window;

error:
    if (settings_window->settings) {
        destroy_settings(settings_window->settings);
    }

    free(settings_window);

    return NULL;
}

void destroy_settings_window(settings_window_t* settings_window) {
    if (!settings_window) {
        return;
    }

    img_window_destroy(settings_window->window);
    free(settings_window);
}

void open_settings_window(settings_window_t* settings_window) {
    if (!settings_window || !settings_window->window) {
        return;
    }

    img_window_set_visible(settings_window->window, true);
}