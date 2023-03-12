#include <string.h>

#include "protocol.h"
#include "utils.h"

#include "command_drls_specific.h"

static int count_parameters(command_parameter_t *parameter) {
    int num = 0;
    while (parameter) {
        num++;
        parameter = parameter->next;
    }
    return num;
}

error_t drls_specific_create(command_drls_t *command, request_t *request) {
    int out_error = ERROR_NONE;
    
    int num_parameters = count_parameters(request->parameters);
    if (num_parameters <= 0) {
        return ERROR_UNSPECIFIC;
    }
    
    command->specific_data.datarefs = create_dynamic_array(sizeof(drls_dataref_t), num_parameters);
    if (!command->specific_data.datarefs) {
        return ERROR_MEMORY_ALLOCATION;
    }
    
    if (!dynamic_array_set_length(command->specific_data.datarefs, num_parameters)) {
        goto error;
    }

    command_parameter_t *parameter = request->parameters;
    int i = 0;
    while (parameter) {
        drls_dataref_t *dataref = dynamic_array_get_pointer(command->specific_data.datarefs, i++);
        if (!dataref) {
            goto error;
        }
        
        int name_length = strlen(parameter->parameter);
        if (name_length < 1) {
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "dataref name is missing");
            goto error;
        }

        dataref->name = copy_string(parameter->parameter);
        if (!dataref->name) {
            out_error = ERROR_MEMORY_ALLOCATION;
            goto error;
        }

        parameter = parameter->next;
    }

    return ERROR_NONE;

 error:
    destroy_dynamic_array(command->specific_data.datarefs);
    command->specific_data.datarefs = NULL;
    return (out_error != ERROR_NONE) ? out_error : ERROR_UNSPECIFIC;
}

void drls_specific_destroy(command_drls_t *command) {
    if (command->specific_data.datarefs) {
        destroy_dynamic_array(command->specific_data.datarefs);
        command->specific_data.datarefs = NULL;
    }
}

void drls_specific_process_flightloop(command_drls_t *command) {
    if (command->specific_data.processed) {
        // flightloop already ran
        return;
    }

    command->timestamp = millis_since_reference(command->session);
    if (command->timestamp < 0) {
        command->failed = true;
        return;
    }
    
    for (int i=0; i<command->specific_data.datarefs->length; i++) {
        drls_dataref_t *dataref = dynamic_array_get_pointer(command->specific_data.datarefs, i);
        if (!dataref) {
            command->failed = true;
            return;
        }

        XPLMDataRef xp_dataref = XPLMFindDataRef(dataref->name);
        if (!xp_dataref) {
            continue;
        }

        dataref->writable = (XPLMCanWriteDataRef(xp_dataref) != 0);
        dataref->types = XPLMGetDataRefTypes(xp_dataref);
        dataref->found = true;

        command->specific_data.num_retrieved++;
    }

    command->specific_data.processed = true;
}

void drls_specific_process_post(command_drls_t *command) {
    if (!command->specific_data.processed) {
        // flightloop needs to run first
        return;
    }

    command->done = true;

    int num_sent = 0;
    error_t err = ERROR_NONE;
    
    if (command->specific_data.num_retrieved <= 0) {
        finish_channel(command->session, command->channel_id, command->timestamp, NULL);
        return;
    }
    
    for (int i=0; i<command->specific_data.datarefs->length; i++) {
        drls_dataref_t *dataref = dynamic_array_get_pointer(command->specific_data.datarefs, i);
        if (!dataref) {
            command->failed = true;
            return;
        }

        if (!dataref->found) {
            continue;
        }

        char *out_types = xprc_encode_types(dataref->types);
        if (!out_types) {
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "failed to encode types");
            command->failed = true;
            return;
        }
        
        const char *out_mode = dataref->writable ? "rw" : "ro";
        char *out = dynamic_sprintf("%s;%s;%s", out_types, out_mode, dataref->name);
        free(out_types);

        if (!out) {
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "failed to encode results");
            command->failed = true;
            return;
        }
        
        bool complete = (command->specific_data.num_retrieved == ++num_sent);
        if (complete) {
            err = finish_channel(command->session, command->channel_id, command->timestamp, out);
        } else {
            err = continue_channel(command->session, command->channel_id, command->timestamp, out);
        }

        free(out);

        if (err != ERROR_NONE) {
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "failed to send results");
            command->failed = true;
            return;
        }
    }
}

