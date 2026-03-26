#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <stdio.h>

#include "utils.h"

#include "requests.h"

#define CHANNEL_ID_LENGTH 4
#define COMMAND_NAME_LENGTH 4
#define MINIMUM_REQUEST_LENGTH (CHANNEL_ID_LENGTH + 1 + COMMAND_NAME_LENGTH)

#define PARAMETER_SEPARATOR ';'
#define OPTION_SEPARATOR ';'
#define OPTION_END_OF_KEY '='
#define REQUEST_PART_SEPARATOR ' '

bool request_has_option(request_t *request, char *name) {
    if (!request || !name) {
        return false;
    }

    command_option_t *option = request->options;
    while (option) {
        if (!strcmp(option->name, name)) {
            return true;
        }
        option = option->next;
    }

    return false;
}

bool request_has_only_options(request_t *request, char **names) {
    if (!request || !names) {
        return false;
    }

    command_option_t *option = request->options;

    // for each option...
    while (option) {
        // ... search the null-terminated array of names
        bool found = false;
        char **name_ref = names;
        while (*name_ref) {
            char *name = *name_ref;
            if (!strcmp(option->name, name)) {
                found = true;
                break;
            }
            name_ref++;
        }

        // ... and abort on first unexpected option
        if (!found) {
            return false;
        }
        
        option = option->next;
    }

    return true;
}

char* request_get_option(request_t *request, char *name, char *default_value) {
    if (!request || !name) {
        return default_value;
    }

    command_option_t *option = request->options;
    while (option) {
        if (!strcmp(option->name, name)) {
            break;
        }
        option = option->next;
    }

    if (option) {
        return option->value;
    }

    return default_value;
}

static error_t add_option(request_t *request, char *name, int name_length, char *value, int value_length) {
    // where should we link the option in the end?
    // if this will be the first option, then it needs to be linked on the request itself
    command_option_t **options_ref = &(request->options);
    if (request->options) {
        // we already recorded other options
        // go to the last element of the list and check that the name is unique
        command_option_t *options = request->options;
        while (options->next) {
            if (!strcmp(name, options->name)) {
                return REQUEST_ERROR_DUPLICATE_OPTION;
            }
            
            options = options->next;
        }
        options_ref = &(options->next);
    }

    command_option_t *new_option = zmalloc(sizeof(command_option_t));
    if (!new_option) {
        return ERROR_MEMORY_ALLOCATION;
    }

    if (!(new_option->name = copy_partial_string(name, name_length))) {
        free(new_option);
        return ERROR_MEMORY_ALLOCATION;
    }

    if (!(new_option->value = copy_partial_string(value, value_length))) {
        free(new_option->name);
        free(new_option);
        return ERROR_MEMORY_ALLOCATION;
    }

    *options_ref = new_option;

    return ERROR_NONE;
}

static error_t add_parameter(request_t *request, char *parameter, int parameter_length) {
    // where should we link the parameter in the end?
    // if this will be the first parameter, then it needs to be linked on the request itself
    command_parameter_t **parameters_ref = &(request->parameters);
    if (request->parameters) {
        // we already recorded other parameters
        // go to the last element of the list
        command_parameter_t *parameters = request->parameters;
        while (parameters->next) {
            parameters = parameters->next;
        }
        
        parameters_ref = &(parameters->next);
    }

    command_parameter_t *new_parameter = zmalloc(sizeof(command_parameter_t));
    if (!new_parameter) {
        return ERROR_MEMORY_ALLOCATION;
    }

    if (!(new_parameter->parameter = copy_partial_string(parameter, parameter_length))) {
        free(new_parameter);
        return ERROR_MEMORY_ALLOCATION;
    }
    
    *parameters_ref = new_parameter;

    return ERROR_NONE;
}

error_t parse_request(request_t **request, char *line, int length) {
    int res;

    if (!request || !line || length < 0) {
        return ERROR_UNSPECIFIC;
    }

    *request = NULL;
    
    if (length < MINIMUM_REQUEST_LENGTH) {
        return REQUEST_ERROR_INVALID_SYNTAX;
    }

    // there has to be a separator after the channel ID
    if (line[CHANNEL_ID_LENGTH] != REQUEST_PART_SEPARATOR) {
        return REQUEST_ERROR_INVALID_SYNTAX;
    }

    // there has to be a separator (part or option) after the command name unless the line terminates early
    if (length > MINIMUM_REQUEST_LENGTH && line[MINIMUM_REQUEST_LENGTH] != REQUEST_PART_SEPARATOR && line[MINIMUM_REQUEST_LENGTH] != OPTION_SEPARATOR) {
        return REQUEST_ERROR_INVALID_SYNTAX;
    }

    channel_id_t channel_id = string_to_channel_id(line);
    if (channel_id == BAD_CHANNEL_ID) {
        return REQUEST_ERROR_INVALID_SYNTAX;
    }

    char *command_name = copy_partial_string(line+CHANNEL_ID_LENGTH+1, COMMAND_NAME_LENGTH);
    if (!command_name) {
        return ERROR_MEMORY_ALLOCATION;
    }
    
    *request = zmalloc(sizeof(request_t));
    if (!*request) {
        free(command_name);
        return ERROR_MEMORY_ALLOCATION;
    }

    (*request)->channel_id = channel_id;
    (*request)->command_name = command_name;

    int offset = MINIMUM_REQUEST_LENGTH + 1;

    // parse command options (key-value map)
    bool has_options = (line[MINIMUM_REQUEST_LENGTH] == OPTION_SEPARATOR);
    if (has_options) {
        int key_start = 0;
        int key_length = 0;
        int value_start = 0;
        int value_length = 0;
        while (offset <= length) {
            char ch = line[offset];
            bool is_end_of_key = !key_length && (ch == OPTION_END_OF_KEY); // only detect if key has not already ended
            bool is_end_of_line = (offset == length);
            bool is_end_of_options = is_end_of_line || (ch == REQUEST_PART_SEPARATOR);
            bool is_end_of_option = is_end_of_options || (ch == OPTION_SEPARATOR);

            if (!key_start) {
                key_start = offset;
            } else if (key_length && !value_start) {
                value_start = offset;
            }
            
            if (is_end_of_option) {
                if (!key_start || !key_length || !value_start) {
                    destroy_request(*request);
                    *request = NULL;
                    return REQUEST_ERROR_INVALID_SYNTAX;
                }
                
                value_length = offset - value_start;
                
                res = add_option(*request, line + key_start, key_length, line + value_start, value_length);
                if (res != ERROR_NONE) {
                    destroy_request(*request);
                    *request = NULL;
                    return res;
                }

                key_start = 0;
                key_length = 0;
                value_start = 0;
                value_length = 0;
            }

            if (is_end_of_key) {
                key_length = offset - key_start;
            }

            if (!is_end_of_line) {
                offset++;
            }
            
            if (is_end_of_options) {
                break;
            }
        }
    }

    // parse command parameters (just split on separator)
    bool has_parameters = (offset <= length);
    if (has_parameters) {
        int parameter_start = 0;
        int parameter_length = 0;
        while (offset <= length) {
            char ch = line[offset];
            bool is_end_of_line = (offset == length);
            bool is_end_of_parameter = is_end_of_line || (ch == PARAMETER_SEPARATOR);

            if (!parameter_start) {
                parameter_start = offset;
            }

            if (is_end_of_parameter) {
                parameter_length = offset - parameter_start;
            
                res = add_parameter(*request, line + parameter_start, parameter_length);
                if (res != ERROR_NONE) {
                    destroy_request(*request);
                    *request = NULL;
                    return res;
                }

                parameter_start = 0;
                parameter_length = 0;
            }

            if (is_end_of_line) {
                break;
            } else {
                offset++;
            }
        }
    }

    return ERROR_NONE;
}

static void destroy_options(command_option_t *options) {
    if (!options) {
        return;
    }

    if (options->next) {
        destroy_options(options->next);
    }

    free(options->name);
    free(options->value);
    free(options);
}

static void destroy_parameters(command_parameter_t *parameters) {
    if (!parameters) {
        return;
    }

    if (parameters->next) {
        destroy_parameters(parameters->next);
    }

    free(parameters->parameter);
    free(parameters);
}

void destroy_request(request_t *request) {
    if (!request) {
        return;
    }

    destroy_options(request->options);
    destroy_parameters(request->parameters);
    free(request->command_name);
    free(request);
}
