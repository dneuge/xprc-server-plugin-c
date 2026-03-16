#include "command_srid.h"

#include <string.h>

#include "_buildinfo.h"
#include "logger.h"
#include "session.h"
#include "utils.h"

#define SRID_COMMAND_VERSION 1

static const char *srid_supported_options[] = {
    NULL
};

static error_t srid_destroy(void *command_ref) {
    RCLOG_WARN("[SRID] unexpected call to destructor (command completes instantly and does not require destruction)");
    
    if (command_ref) {
        RCLOG_WARN("[SRID] attempted destruction with unexpected command_ref %p, possible memory corruption", command_ref);
        return ERROR_UNSPECIFIC;
    }
    
    return ERROR_NONE;
}

static error_t srid_terminate(void *command_ref) {
    RCLOG_WARN("[SRID] unexpected request to terminate (command completes instantly and does not require termination/destruction)");
    
    if (command_ref) {
        RCLOG_WARN("[SRID] attempted termination with unexpected command_ref %p, possible memory corruption", command_ref);
        return ERROR_UNSPECIFIC;
    }

    return ERROR_NONE;
}

static error_t srid_create(void **command_ref, session_t *session, request_t *request, command_config_t *config) {
    error_t out_error = ERROR_NONE;
    
    channel_id_t channel_id = request->channel_id;
    
    if (config->version != SRID_COMMAND_VERSION) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unexpected command version");
        return ERROR_UNSPECIFIC;
    }

    *command_ref = NULL;

    if (!request_has_only_options(request, (char**)srid_supported_options)) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unsupported options");
        return ERROR_UNSPECIFIC;
    }
    
    command_parameter_t *parameter = request->parameters;
    if (parameter) {
        error_channel(session, channel_id, CURRENT_TIME_REFERENCE, "unsupported parameters");
        return ERROR_UNSPECIFIC;
    }

    // confirm channel without data on first message for better readability of raw transcripts
    confirm_channel(session, channel_id, CURRENT_TIME_REFERENCE, NULL);

    continue_channel(session, channel_id, CURRENT_TIME_REFERENCE, "id:" XPRC_SERVER_ID);
    continue_channel(session, channel_id, CURRENT_TIME_REFERENCE, "name:" XPRC_SERVER_NAME);
    continue_channel(session, channel_id, CURRENT_TIME_REFERENCE, "version:" XPRC_SERVER_VERSION);

    if (strlen(XPRC_SERVER_WEBSITE) > 0) {
        continue_channel(session, channel_id, CURRENT_TIME_REFERENCE, "website:" XPRC_SERVER_WEBSITE);
    }

    char *msg_apiversion = dynamic_sprintf("apiversion:%d", session->server->config.xpinfo.xplm_version);
    if (!msg_apiversion) {
        RCLOG_WARN("[SRID] failed to format X-Plane API version");
        out_error = ERROR_INCOMPLETE;
    } else {
        continue_channel(session, channel_id, CURRENT_TIME_REFERENCE, msg_apiversion);
        free(msg_apiversion);
        msg_apiversion = NULL;
    }

    char *msg_xpversion = dynamic_sprintf("xpversion:%d", session->server->config.xpinfo.xplane_version);
    if (!msg_xpversion) {
        RCLOG_WARN("[SRID] failed to format X-Plane version");
        out_error = ERROR_INCOMPLETE;
    } else {
        continue_channel(session, channel_id, CURRENT_TIME_REFERENCE, msg_xpversion);
        free(msg_xpversion);
        msg_xpversion = NULL;
    }

    if (strlen(XPRC_SERVER_BUILD_ID) > 0) {
        continue_channel(session, channel_id, CURRENT_TIME_REFERENCE, "x-build-id:" XPRC_SERVER_BUILD_ID);
    }

    if (strlen(XPRC_SERVER_BUILD_REF) > 0) {
        continue_channel(session, channel_id, CURRENT_TIME_REFERENCE, "x-build-ref:" XPRC_SERVER_BUILD_REF);
    }

    if (strlen(XPRC_SERVER_BUILD_TIME) > 0) {
        continue_channel(session, channel_id, CURRENT_TIME_REFERENCE, "x-build-time:" XPRC_SERVER_BUILD_TIME);
    }

    finish_channel(session, channel_id, CURRENT_TIME_REFERENCE, "x-build-target:" XPRC_SERVER_BUILD_TARGET);

    return out_error;
}

static command_config_t* srid_create_default_config() {
    return create_command_config(SRID_COMMAND_VERSION);
}

static error_t srid_merge_config(command_config_t **new_config, char **err_msg, command_config_t *previous_config, command_config_t *requested_changes) {
    if (requested_changes->version != SRID_COMMAND_VERSION) {
        *err_msg = dynamic_sprintf("only supported version is %u, requested %u", SRID_COMMAND_VERSION, requested_changes->version);
        return ERROR_UNSPECIFIC;
    }

    if (has_command_feature_flags(requested_changes)) {
        *err_msg = dynamic_sprintf("current command implementation does not support any feature flags");
        return ERROR_UNSPECIFIC;
    }

    return ERROR_NONE;
}

command_t command_srid = {
    .name = "SRID",
    .create = srid_create,
    .terminate = srid_terminate,
    .destroy = srid_destroy,
    .create_default_config = srid_create_default_config,
    .merge_config = srid_merge_config,
};
