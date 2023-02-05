#ifndef SESSION_H
#define SESSION_H

#include <time.h>

#include "channels.h"

typedef struct {
    struct timespec reference_time;
    channels_table_t *channels;
} session_t;

#endif
