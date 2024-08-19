#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logger.h"
#include "requests.h"

int main(int argc, char **argv) {
    if (argc != 4) {
        printf("request parser test; call with: offset, length, content\n");
        return 1;
    }

    xprc_log_init();
    xprc_set_min_log_level_console(RCLOG_LEVEL_TRACE);

    int offset = atoi(argv[1]);
    int length = atoi(argv[2]);
    char *content = argv[3];

    if (length < 0) {
        length = strlen(content) - offset;
    }

    RCLOG_INFO("offset %d, length %d", offset, length);

    request_t *request = NULL;
    int res = parse_request(&request, content+offset, length);
    if (res != ERROR_NONE) {
        RCLOG_ERROR("error: %d", res);
        return 1;
    }
    
    if (!request) {
        RCLOG_ERROR("no error indicated but result is NULL");
        return 1;
    }

    char channel_id_string[5] = {0};
    channel_id_to_string(request->channel_id, channel_id_string);
    
    RCLOG_INFO("Channel: %s %d", channel_id_string, request->channel_id);
    RCLOG_INFO("Command: %s", request->command_name);

    if (!request->options) {
        RCLOG_INFO("No options");
    } else {
        RCLOG_INFO("Options:");
        command_option_t *option = request->options;
        while (option) {
            RCLOG_INFO("\"%s\": \"%s\"", option->name, option->value);
            option = option->next;
        }
    }

    if (!request->parameters) {
        RCLOG_INFO("No parameters");
    } else {
        command_parameter_t *parameter = request->parameters;
        int i=1;
        
        RCLOG_INFO("Parameters:");
        while (parameter) {
            RCLOG_INFO("#%d: \"%s\"", i++, parameter->parameter);
            parameter = parameter->next;
        }
    }

    RCLOG_INFO("Done");

    destroy_request(request);
    
    xprc_log_destroy();

    return 0;
}
