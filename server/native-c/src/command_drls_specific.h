#ifndef COMMAND_DRLS_SPECIFIC_H
#define COMMAND_DRLS_SPECIFIC_H

#include <stdbool.h>

#include <XPLMDataAccess.h>

#include "arrays.h"

typedef struct {
    char *name;
    XPLMDataTypeID types;
    bool writable;
    bool found;
} drls_dataref_t;

typedef struct {
    dynamic_array_t *datarefs;
    int num_retrieved;
    bool processed;
} drls_specific_t;

#include "command_drls.h"

error_t drls_specific_create(command_drls_t *command, request_t *request);
void drls_specific_destroy(command_drls_t *command);

void drls_specific_process_flightloop(command_drls_t *command);
void drls_specific_process_post(command_drls_t *command);

#endif
