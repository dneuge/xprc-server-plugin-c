#ifndef COMMANDS_H
#define COMMANDS_H

typedef struct _command_factory_t command_factory_t;

#include "command.h"
#include "channels.h"
#include "hashmap.h"

typedef struct _command_factory_t {
    hashmap_t *commands_by_name;
} command_factory_t;

command_factory_t* create_command_factory();
void destroy_command_factory(command_factory_t *factory);

error_t create_command(command_factory_t *factory, channel_t *channel, session_t *session, request_t *request);

#endif
