#ifndef SETTINGS_WINDOW_H
#define SETTINGS_WINDOW_H

#include "img_window.h"

typedef struct {
    img_window window;

    bool dirty;
    bool valid;

    // settings
    bool auto_startup;
    bool auto_regen_password;
    char *network_interface;
    int network_port;
    bool network_enable_ipv6;

    // UI
    bool reveal_password;

    int network_port_min;
    int network_port_max;

    bool btn_pwd_copy_state;
    bool btn_pwd_reveal_state;
    bool btn_pwd_regen_state;

    bool btn_network_reset_state;

    bool btn_apply_state;
    bool btn_discard_state;
} settings_window_t;

settings_window_t* create_settings_window();
void destroy_settings_window(settings_window_t* settings_window);

#endif
