#ifndef COMMAND_DRLS_UNSPECIFIC_H
#define COMMAND_DRLS_UNSPECIFIC_H

#include <stdbool.h>

#ifdef XPLM400
#define COMMAND_DRLS_UNSPECIFIC_SUPPORTED true
#include "command_drls_unspecific_impl.h"
#else
#define COMMAND_DRLS_UNSPECIFIC_SUPPORTED false
#include "command_drls_unspecific_dummy.h"
#endif

#include "command_drls.h"

error_t drls_unspecific_create(command_drls_t *command);
void drls_unspecific_destroy(command_drls_t *command);

void drls_unspecific_process_flightloop(command_drls_t *command);
void drls_unspecific_process_post(command_drls_t *command);

#endif
