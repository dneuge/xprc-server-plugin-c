#include "command_drls_unspecific.h"
#include "command_drls_unspecific_dummy.h"

error_t drls_unspecific_create(command_drls_t *command) {
    return ERROR_UNSPECIFIC;
}

void drls_unspecific_destroy(command_drls_t *command) {
    // nothing to do
}

void drls_unspecific_process_flightloop(command_drls_t *command) {
    command->failed = true;
}

void drls_unspecific_process_post(command_drls_t *command) {
    command->failed = true;
}
