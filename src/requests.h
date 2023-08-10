#ifndef REQUESTS_H
#define REQUESTS_H

/**
 * @file requests.h XPRC request decoding
 *
 * All (non-empty) lines sent by clients within a session after the handshake/authentication are requests for a command,
 * including a channel ID chosen by the client and optionally including command parameters and options.
 *
 * #parse_request() decodes such a line into a #request_t instance that provides easy access to all that information.
 * The server will perform the low-level handling and pass the request to the command factory (see commands.h) through
 * which commands will receive the same request before it gets eventually destroyed when the line has been processed.
 */

typedef struct _request_t request_t;

#include "errors.h"
#include "channels.h"

/// indicates a general syntax error during request parsing
#define REQUEST_ERROR_INVALID_SYNTAX (REQUEST_ERROR_BASE + 0)
/// indicates that a command option was specified twice in the same request
#define REQUEST_ERROR_DUPLICATE_OPTION (REQUEST_ERROR_BASE + 1)

/// an option to the command being requested as a key-value pair
typedef struct _command_option_t command_option_t;
typedef struct _command_option_t {
    /// name of the option (null-terminated string)
    char *name;
    /// value of the option (null-terminated string)
    char *value;
    /// the next option in the same request; NULL if no more options were specified
    command_option_t *next;
} command_option_t;

/// a parameter to the command being requested
typedef struct _command_parameter_t command_parameter_t;
typedef struct _command_parameter_t {
    /// the parameter (null-terminated string)
    char *parameter;
    /// the next parameter in the same request; NULL if no more parameters were specified
    command_parameter_t *next;
} command_parameter_t;

/// a parsed XPRC request
typedef struct _request_t {
    /// the channel ID as requested by the client
    channel_id_t channel_id;
    /// the name of the command being requested by the client
    char *command_name;
    /// all options attached to the request
    command_option_t *options;
    /// all parameters attached to the request
    command_parameter_t *parameters;
} request_t;

/**
 * Parses the given XPRC protocol encoded string to a request.
 * @param request where to store the parsed request
 * @param line XPRC protocol encoded request line to parse (does not need to be null-terminated; length to parse is provided separately)
 * @param length number of characters to parse from the provided line string
 * @return error code; #ERROR_NONE if successful
 */
error_t parse_request(request_t **request, char *line, int length);
/**
 * Destroys a request instance.
 * @param request instance to destroy
 */
void destroy_request(request_t *request);

/**
 * Tests if the given option has been provided in a request.
 * @param request request to test
 * @param name option name to check existence of
 * @return true if the request has the option set, false if not
 */
bool request_has_option(request_t *request, char *name);
/**
 * Tests if only the specified options were provided in a request; the options are also allowed to be missing but no
 * other options are allowed.
 * @param request request to test
 * @param names null-terminated array of null-terminated strings of allowed option names
 * @return true if the request does not contain any unwanted extra options, false if it has unwanted extra options
 */
bool request_has_only_options(request_t *request, char **names);

/**
 * Retrieves the value (shared null-terminated string) of the specified option or the default value if it is missing.
 * @param request request to retrieve option from
 * @param name name of the option to retrieve the value for
 * @param default_value will be returned in case the option has not been set
 * @return shared null-terminated string of the option's value, default value if not set; must not be freed
 */
char* request_get_option(request_t *request, char *name, char *default_value);

#endif
