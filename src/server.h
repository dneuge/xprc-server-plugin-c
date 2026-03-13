#ifndef SERVER_H
#define SERVER_H

/**
 * @file server.h XPRC network server
 */

#include <stdbool.h>

#include "threads_compat.h"

typedef struct _server_t server_t;

#include "commands.h"
#include "lists.h"
#include "network.h"
#include "task_schedule.h"

#ifdef BUILD_PLUGIN
#include "dataproxy.h"
#include "xpcommands.h"
#include "xpqueue.h"
#else
// in case we are not building the plugin (but test code/utils)
// we need to stub unused type definitions instead of importing
// plugin code to break compile-time dependency on those files
typedef struct {} dataproxy_registry_t;
typedef struct {} xpcommand_registry_t;
typedef struct {} xpqueue_t;
#endif

/// general information about X-Plane
typedef struct {
    /// X-Plane version ID as returned by XPLMGetVersions
    int xplane_version;
    /// X-Plane API (XPLM) version ID as returned by XPLMGetVersions
    int xplm_version;
} xpinfo_t;

/// resources and configuration passed to the server
typedef struct {
    /// password required to match for session authentication
    char *password;
    /// network configuration
    network_server_config_t network;

    /// schedule to queue tasks onto; scheduler is implemented by the plugin outside the server
    task_schedule_t *task_schedule;
    /// used to instantiate new commands for channels
    command_factory_t *command_factory;
    /// used to register hosted datarefs
    dataproxy_registry_t *dataproxy_registry;
    /// used to register hosted X-Plane commands
    xpcommand_registry_t *xpcommand_registry;
    /// used to queue one-shot X-Plane commands
    xpqueue_t *xpqueue;

    /// general information about X-Plane
    xpinfo_t xpinfo;
} server_config_t;

/**
 * Copies the given server configuration. Linked instances are only copied as pointers at time of copy creation
 * and will be shared.
 * @param source configuration to copy from
 * @return copied configuration; NULL on error
 */
server_config_t* copy_server_config(server_config_t *source);

/**
 * Frees the password and server configuration structure. Anything else (linked instances) remains in memory
 * and has to be managed separately.
 * @param config configuration to destroy
 */
void destroy_server_config(server_config_t *config);

/// an XPRC server instance
typedef struct _server_t {
    /// configuration and resources provided to the instance at time of creation
    server_config_t config;
    /// TCP network server implementation
    network_server_t *network;

    /// currently open sessions
    list_t *sessions;
    /// synchronization across sessions list
    mtx_t mutex;
} server_t;

/**
 * Starts a new XPRC server instance.
 * @param server will be set to the created server
 * @param config configuration and resources to use for this instance
 * @return error code; #ERROR_NONE on success
 */
error_t start_server(server_t **server, server_config_t *config);
/**
 * Stops the XPRC server, closing all sessions and terminating all commands.
 * @param server server that should be stopped
 * @return error code; #ERROR_NONE on success
 */
error_t stop_server(server_t *server);

/**
 * Performs maintenance operations; should be called periodically while server is running.
 * @param server server to perform maintenance for
 * @return error code; #ERROR_NONE on success
 */
error_t maintain_server(server_t *server);

/**
 * Registers a new session to the server so it can be maintained and tracked.
 * @param server server to register session on
 * @param session session to be registered
 * @return error code; #ERROR_NONE on success
 */
error_t register_session(server_t *server, session_t *session);

/**
 * Detaches a session from the server.
 * @param server server to detach session from
 * @param session session to be detached
 * @return error code; #ERROR_NONE on success
 */
error_t unregister_session(server_t *server, session_t *session);

#endif
