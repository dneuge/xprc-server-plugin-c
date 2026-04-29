#include <XPLMDataAccess.h>

#include "logger.h"
#include "protocol.h"
#include "utils.h"

#include "command_drls.h"
#include "command_drls_unspecific_impl.h"

static void process_count(command_drls_t *command) {
    // "count" stage is run within flight loop callback
    // the goal is to just retrieve the preliminary number of datarefs
    // to be able to asynchronously prepare data structures for retrieval
    // in the next stage

    command->unspecific_data.count = XPLMCountDataRefs();
    
    command->unspecific_data.stage = DRLS_UNSPECIFIC_STAGE_PREPARE;
}

static bool format_dataref_infos(dynamic_array_t *arr, int from, int toExclusive) {
    for (int i=from; i<toExclusive; i++) {
        XPLMDataRefInfo_t *xp_dataref = dynamic_array_get_pointer(arr, i);
        if (!xp_dataref) {
            return false;
        }
        
        xp_dataref->structSize = sizeof(XPLMDataRefInfo_t);
    }

    return true;
}

static void process_prepare(command_drls_t *command) {
    // "prepare" stage is run after flight loop by post-processing thread
    // the goal is to preallocate and format the array to be used for
    // data retrieval in the next stage

    if (command->unspecific_data.count <= 0) {
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "X-Plane API reported zero datarefs");
        command->failed = true;
        return;
    }

    // dataref array
    command->unspecific_data.datarefs = create_dynamic_array(sizeof(XPLMDataRef), command->unspecific_data.count);
    if (!command->unspecific_data.datarefs) {
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "failed to allocate dataref array");
        command->failed = true;
        return;
    }

    if (!dynamic_array_set_length(command->unspecific_data.datarefs, command->unspecific_data.count)) {
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "failed to set dataref array length");
        command->failed = true;
        return;
    }

    // info array
    command->unspecific_data.dataref_infos = create_dynamic_array(sizeof(XPLMDataRefInfo_t), command->unspecific_data.count);
    if (!command->unspecific_data.dataref_infos) {
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "failed to allocate info array");
        command->failed = true;
        return;
    }

    if (!dynamic_array_set_length(command->unspecific_data.dataref_infos, command->unspecific_data.count)) {
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "failed to set info array length");
        command->failed = true;
        return;
    }

    if (!format_dataref_infos(command->unspecific_data.dataref_infos, 0, command->unspecific_data.count)) {
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "failed to format info array");
        command->failed = true;
        return;
    }
    command->unspecific_data.num_prepared = command->unspecific_data.count;
    
    command->unspecific_data.stage = DRLS_UNSPECIFIC_STAGE_RETRIEVE;
}

static void process_retrieve(command_drls_t *command) {
    // "retrieve" stage is run within flight loop callback
    // the goal is to retrieve all datarefs by requesting X-Plane to fill
    // all prepared data structures
    // number of datarefs might have changed from "count" stage so the array
    // needs to be extended (and formatted) if necessary

    command->timestamp = millis_since_reference(command->session);
    if (command->timestamp < 0) {
        command->failed = true;
        return;
    }

    command->unspecific_data.count = XPLMCountDataRefs();
    if (command->unspecific_data.count <= 0) {
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "X-Plane API reported zero datarefs");
        command->failed = true;
        return;
    }
    
    if (!dynamic_array_set_length(command->unspecific_data.datarefs, command->unspecific_data.count)) {
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "failed to resize datarefs array");
        command->failed = true;
        return;
    }

    if (!dynamic_array_set_length(command->unspecific_data.dataref_infos, command->unspecific_data.count)) {
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "failed to resize info array");
        command->failed = true;
        return;
    }

    if (command->unspecific_data.count > command->unspecific_data.num_prepared) {
        // not all dataref info blocks were prepared in advance, we need to format the newly added ones
        if (!format_dataref_infos(command->unspecific_data.datarefs, command->unspecific_data.num_prepared, command->unspecific_data.count)) {
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "failed to format additional info array entries");
            command->failed = true;
            return;
        }
    }

    XPLMGetDataRefsByIndex(0, command->unspecific_data.count, command->unspecific_data.datarefs->data);

    for (int i=0; i<command->unspecific_data.count; i++) {
        XPLMDataRef *xp_dataref = dynamic_array_get_pointer(command->unspecific_data.datarefs, i);
        if (!xp_dataref) {
            command->failed = true;
            return;
        }
        
        XPLMDataRefInfo_t *xp_info = dynamic_array_get_pointer(command->unspecific_data.dataref_infos, i);
        if (!xp_info) {
            command->failed = true;
            return;
        }
        
        XPLMGetDataRefInfo(*xp_dataref, xp_info);
    }
    
    command->unspecific_data.stage = DRLS_UNSPECIFIC_STAGE_SUBMIT;
}

static void process_submit(command_drls_t *command) {
    // "submit" stage is run after flight loop by post-processing thread
    // the goal is to send all dataref information to the XPRC channel

    command->done = true;
    
    int num_valid = 0;
    for (int i=0; i<command->unspecific_data.count; i++) {
        XPLMDataRefInfo_t *xp_info = dynamic_array_get_pointer(command->unspecific_data.dataref_infos, i);
        if (!xp_info) {
            command->failed = true;
            return;
        }

        if (xp_info->structSize != sizeof(XPLMDataRefInfo_t)) {
            RCLOG_WARN("[DRLS unspecific] mismatch in XPLMDataRefInfo_t.structSize: expected %zu, got %d", sizeof(XPLMDataRefInfo_t), xp_info->structSize);
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "X-Plane API corrupted datarefs array");
            command->failed = true;
            return;
        }
        
        if (xp_info->name) {
            num_valid++;
        }
    }

    if (num_valid <= 0) {
        error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "X-Plane API did not list any datarefs");
        command->failed = true;
        return;
    }

    int num_sent = 0;
    error_t err = ERROR_NONE;
    
    for (int i=0; i<command->unspecific_data.count; i++) {
        XPLMDataRefInfo_t *xp_info = dynamic_array_get_pointer(command->unspecific_data.dataref_infos, i);
        if (!xp_info) {
            command->failed = true;
            return;
        }
        
        if (!xp_info->name) {
            continue;
        }

        char *out_types = xprc_encode_types(xp_info->type);
        if (!out_types) {
            RCLOG_WARN("[DRLS unspecific] types %d could not be encoded for %s", xp_info->type, xp_info->name);
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "failed to encode types");
            command->failed = true;
            return;
        }

        const char *out_mode = xp_info->writable ? "rw" : "ro";
        char *out = dynamic_sprintf("%s;%s;%s", out_types, out_mode, xp_info->name);
        free(out_types);
        
        if (!out) {
            error_channel(command->session, command->channel_id, CURRENT_TIME_REFERENCE, "failed to encode results");
            command->failed = true;
            return;
        }
        
        bool complete = (num_valid == ++num_sent);
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

error_t drls_unspecific_create(command_drls_t *command) {
    command->unspecific_data.stage = DRLS_UNSPECIFIC_STAGE_COUNT;
    return ERROR_NONE;
}

void drls_unspecific_destroy(command_drls_t *command) {
    if (command->unspecific_data.datarefs) {
        destroy_dynamic_array(command->unspecific_data.datarefs);
        command->unspecific_data.datarefs = NULL;
    }
    
    if (command->unspecific_data.dataref_infos) {
        destroy_dynamic_array(command->unspecific_data.dataref_infos);
        command->unspecific_data.dataref_infos = NULL;
    }
}

void drls_unspecific_process_flightloop(command_drls_t *command) {
    if (command->failed || command->done) {
        return;
    }
    
    switch(command->unspecific_data.stage) {
    case DRLS_UNSPECIFIC_STAGE_COUNT:
        process_count(command);
        break;
        
    case DRLS_UNSPECIFIC_STAGE_RETRIEVE:
        process_retrieve(command);
        break;
        
    default:
        break;
    }
}

void drls_unspecific_process_post(command_drls_t *command) {
    if (command->failed || command->done) {
        return;
    }
    
    switch(command->unspecific_data.stage) {
    case DRLS_UNSPECIFIC_STAGE_PREPARE:
        process_prepare(command);
        break;

    case DRLS_UNSPECIFIC_STAGE_SUBMIT:
        process_submit(command);
        break;

    default:
        break;
    }
}
