#ifndef SETTINGS_WINDOW_H
#define SETTINGS_WINDOW_H

#include "img_window.h"

typedef struct {
    img_window window;
} settings_window_t;

settings_window_t* create_settings_window();
void destroy_settings_window(settings_window_t* settings_window);

#endif
