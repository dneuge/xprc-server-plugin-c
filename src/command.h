#ifndef COMMAND_H
#define COMMAND_H

typedef struct _command_t command_t;

#include "errors.h"
#include "requests.h"
#include "session.h"

typedef error_t (*command_create_f) (void **command_ref, session_t *session, request_t *request);
typedef error_t (*command_terminate_f) (void *command_ref);
typedef error_t (*command_destroy_f) (void *command_ref);

typedef struct _command_t {
    char *name;
    command_create_f create;
    command_terminate_f terminate;
    command_destroy_f destroy;
} command_t;

#endif
