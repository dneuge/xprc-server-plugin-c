#include <stdlib.h>
#include <string.h>

#include "gui_utils.h"
#include "../network.h"
#include "../utils.h"

#include "settings_window.h"

#define NETWORK_INTERFACE_SIZE 256

static char pwd[] = "abcdefg"; // DEBUG

static void limit_network_port(settings_window_t *settings_window) {
    if (settings_window->network_port < NETWORK_MINIMUM_PORT) {
        settings_window->network_port = NETWORK_MINIMUM_PORT;
    } else if (settings_window->network_port > NETWORK_MAXIMUM_PORT) {
        settings_window->network_port = NETWORK_MAXIMUM_PORT;
    }
}

static void validate(settings_window_t *settings_window) {
    settings_window->valid = (settings_window->network_port >= NETWORK_MINIMUM_PORT)
                             && (settings_window->network_port <= NETWORK_MAXIMUM_PORT)
                             && (strlen(settings_window->network_interface) > 0) // FIXME: improve check
            ;
}

static void copy_from_settings(settings_window_t *settings_window) {
    settings_window->auto_startup = true; // FIXME: get from actual settings

    settings_window->auto_regen_password = true; // FIXME: get from actual settings

    strncpy(settings_window->network_interface, "localhost", NETWORK_INTERFACE_SIZE); // FIXME: get from actual settings
    settings_window->network_port = 1234; // FIXME: get from actual settings
    limit_network_port(settings_window);
    settings_window->network_enable_ipv6 = false; // FIXME: get from network implementation

    settings_window->dirty = false;

    validate(settings_window);
}

static void reset_network_settings(settings_window_t *settings_window) {
    // FIXME: define and use global constants
    settings_window->network_port = 3461;
    strncpy(settings_window->network_interface, "localhost", NETWORK_INTERFACE_SIZE);
    settings_window->network_enable_ipv6 = false;
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

static void update_view(settings_window_t *settings_window) {
    size_t pwd_size = strlen(pwd) + 1; // DEBUG

    float label_width = 80.0f;
    float input_offset_x = 8.0f;
    float button_spacing = 2.0f;

    settings_window->dirty |= igCheckbox("start XPRC automatically", &settings_window->auto_startup);

    igText(" ");

    igText("Password:");
    igSameLine(label_width + input_offset_x, 0.0f);
    ImGuiInputTextFlags password_text_flags = settings_window->reveal_password ? ImGuiInputTextFlags_ReadOnly
                                                                               : ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_Password;
    igInputText("##password", pwd, pwd_size, password_text_flags, NULL, NULL);
    igIndent(label_width);
    settings_window->btn_pwd_copy_state = igButton("Copy", IMGUI_ZERO_SIZE);
    igSameLine(0.0f, button_spacing);
    char *btn_reveal_text = settings_window->reveal_password ? "Hide" : "Reveal";
    settings_window->btn_pwd_reveal_state = igButton(btn_reveal_text, IM_VEC2(80, 0));
    igSameLine(0.0f, button_spacing);
    settings_window->btn_pwd_regen_state = igButton("Regenerate", IMGUI_ZERO_SIZE);
    settings_window->dirty |= igCheckbox("automatically regenerate on each start", &settings_window->auto_regen_password);
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
        settings_window->dirty |= igInputText("##interface", settings_window->network_interface, NETWORK_INTERFACE_SIZE,
                                              ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CharsNoBlank |
                                              ImGuiInputTextFlags_CallbackEdit, imgui_input_set_dirty, settings_window);

        igText("Port:");
        igSameLine(label_width + indent_section + input_offset_x, 0.0f);
        settings_window->dirty |= igInputInt("##port", &settings_window->network_port, 1, 10,ImGuiInputTextFlags_CharsDecimal);

        settings_window->dirty |= igCheckbox("listen on IPv6", &settings_window->network_enable_ipv6);

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
    limit_network_port(settings_window);

    if (settings_window->btn_pwd_reveal_state) {
        settings_window->reveal_password = !settings_window->reveal_password;
    }

    if (settings_window->btn_pwd_regen_state) {
        // FIXME: regenerate password
        settings_window->dirty = true;
    }

    if (settings_window->btn_network_reset_state) {
        reset_network_settings(settings_window);
        settings_window->dirty = true;
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

settings_window_t* create_settings_window() {
    settings_window_t *settings_window = zalloc(sizeof(settings_window_t));
    if (!settings_window) {
        return NULL;
    }

    settings_window->network_interface = zalloc(NETWORK_INTERFACE_SIZE);
    if (!settings_window->network_interface) {
        free(settings_window);
        return NULL;
    }

    // copy settings initially before we start defining the UI, just in case (will be updated again before opening the
    // window)
    copy_from_settings(settings_window);

    settings_window->window = create_centered_window(655, 485,imgui_update, imgui_show, settings_window);
    if (!settings_window->window) {
        printf("[XPRC] failed to create settings window\n");
        free(settings_window);
        return NULL;
    }

    img_window_set_title(settings_window->window, "XPRC Settings");

    return settings_window;
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