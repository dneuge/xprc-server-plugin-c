#ifndef COMMAND_DRLS_H
#define COMMAND_DRLS_H

#include "command.h"

typedef struct _command_drls_t command_drls_t;

#include "command_drls_specific.h"
#include "command_drls_unspecific.h"

#define TYPE_SPECIFIC 0
#define TYPE_UNSPECIFIC 1

typedef uint8_t drls_type;

typedef struct _command_drls_t {
    session_t *session;
    channel_id_t channel_id;
    
    task_t *task;
    
    bool failed;
    bool done;

    int64_t timestamp;
    
    drls_type type;
    union {
        drls_unspecific_t unspecific_data;
        drls_specific_t specific_data;
    };
} command_drls_t;

extern command_t command_drls;

#endif
