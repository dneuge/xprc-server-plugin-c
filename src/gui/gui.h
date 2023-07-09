#ifndef GUI_H
#define GUI_H

#include <stdbool.h>

typedef struct _gui_t gui_t;

#include "settings_window.h"

typedef struct _gui_t {
    settings_window_t *settings_window;
} gui_t;

gui_t* gui_create();
void gui_destroy(gui_t *gui);

#endif
