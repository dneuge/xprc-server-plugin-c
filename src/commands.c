#include <stdlib.h>
#include <string.h>

#include "commands.h"

#include "command_cmrg.h"
#include "command_drci.h"
#include "command_drls.h"
#include "command_drqv.h"
#include "command_drmu.h"

static bool register_command(command_factory_t *factory, command_t *command) {
    void *_old_value = NULL;
    return hashmap_put(factory->commands_by_name, command->name, command, &_old_value);
}

command_factory_t* create_command_factory() {
    command_factory_t *factory = malloc(sizeof(command_factory_t));
    if (!factory) {
        return NULL;
    }

    memset(factory, 0, sizeof(command_factory_t));

    factory->commands_by_name = create_hashmap();
    if (!factory->commands_by_name) {
        free(factory);
        return NULL;
    }

    bool success = true;
    success &= register_command(factory, &command_cmrg);
    success &= register_command(factory, &command_drci);
    success &= register_command(factory, &command_drls);
    success &= register_command(factory, &command_drqv);
    success &= register_command(factory, &command_drmu);

    if (!success) {
        destroy_command_factory(factory);
        return NULL;
    }

    return factory;
}

void destroy_command_factory(command_factory_t *factory) {
    destroy_hashmap(factory->commands_by_name, NULL);
    free(factory);
}

error_t create_command(command_factory_t *factory, channel_t *channel, session_t *session, request_t *request) {
    command_t *command = hashmap_get(factory->commands_by_name, request->command_name);
    if (!command) {
        return ERROR_UNSPECIFIC;
    }

    channel->command = command;
    
    return command->create(&channel->command_ref, session, request);
}
