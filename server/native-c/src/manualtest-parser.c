#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "requests.h"

int main(int argc, char **argv) {
    if (argc != 4) {
        printf("request parser test; call with: offset, length, content\n");
        return 1;
    }

    int offset = atoi(argv[1]);
    int length = atoi(argv[2]);
    char *content = argv[3];

    if (length < 0) {
        length = strlen(content) - offset;
    }

    printf("offset %d, length %d\n", offset, length);

    request_t *request = NULL;
    int res = parse_request(&request, content+offset, length);
    if (res != ERROR_NONE) {
        printf("error: %d\n", res);
        return 1;
    }
    
    if (!request) {
        printf("no error indicated but result is NULL\n");
        return 1;
    }

    char channel_id_string[5] = {0};
    channel_id_to_string(request->channel_id, channel_id_string);
    
    printf("Channel: %s %d\n", channel_id_string, request->channel_id);
    printf("Command: %s\n", request->command_name);
    printf("\n");

    if (!request->options) {
        printf("No options\n");
    } else {
        printf("Options:\n");
        command_option_t *option = request->options;
        while (option) {
            printf("\"%s\": \"%s\"\n", option->name, option->value);
            option = option->next;
        }
    }
    printf("\n");

    if (!request->parameters) {
        printf("No parameters\n");
    } else {
        command_parameter_t *parameter = request->parameters;
        int i=1;
        
        printf("Parameters:\n");
        while (parameter) {
            printf("#%d: \"%s\"\n", i++, parameter->parameter);
            parameter = parameter->next;
        }
    }

    printf("\n");
    printf("Done\n");

    destroy_request(request);
    
    return 0;
}
