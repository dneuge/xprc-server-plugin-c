#ifndef REQUESTS_H
#define REQUESTS_H

#include "errors.h"
#include "channels.h"

#define REQUEST_ERROR_INVALID_SYNTAX (REQUEST_ERROR_BASE + 0)
#define REQUEST_ERROR_DUPLICATE_OPTION (REQUEST_ERROR_BASE + 1)

typedef struct _command_option_t command_option_t;
typedef struct _command_option_t {
    char *name;
    char *value;
    command_option_t *next;
} command_option_t;

typedef struct _command_parameter_t command_parameter_t;
typedef struct _command_parameter_t {
    char *parameter;
    command_parameter_t *next;
} command_parameter_t;

typedef struct {
    channel_id_t channel_id;
    char *command_name;
    command_option_t *options;
    command_parameter_t *parameters;
} request_t;

int parse_request(request_t **request, char *line, int length);
void destroy_request(request_t *request);

#endif
