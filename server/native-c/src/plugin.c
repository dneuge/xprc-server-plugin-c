#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <XPLMPlugin.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>

#include "lists.h"
#include "server.h"

#define CALL_ON_NEXT_FRAME -1.0f

XPLMFlightLoopID flight_loop_before_flight_model_id = {0};
XPLMFlightLoopID flight_loop_after_flight_model_id = {0};
bool flight_loop_registered = false;

server_t *server = NULL;
server_config_t server_config = {0};
bool server_started = false;

mtx_t task_queue_mutex;
bool has_task_queue_mutex = false;
bool locked_task_queue_mutex = false;
prealloc_list_t *task_queue_before_flight_model = NULL;
prealloc_list_t *task_queue_after_flight_model = NULL;

bool fatal_error = false;

static float process_flight_loop_before_flight_model(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon) {
    if (mtx_lock(&task_queue_mutex) != thrd_success) {
        // TODO: is there any way we could log that?
        return CALL_ON_NEXT_FRAME;
    }

    locked_task_queue_mutex = true;
    
    return CALL_ON_NEXT_FRAME;
}

static float process_flight_loop_after_flight_model(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon) {
    if (!locked_task_queue_mutex) {
        return CALL_ON_NEXT_FRAME;
    }

    locked_task_queue_mutex = false;
    mtx_unlock(&task_queue_mutex);
    
    return CALL_ON_NEXT_FRAME;
}

static void register_flight_loop(XPLMFlightLoopPhaseType phase, XPLMFlightLoop_f callback, XPLMFlightLoopID *flight_loop_id) {
    XPLMCreateFlightLoop_t params = {
        .structSize = sizeof(XPLMCreateFlightLoop_t),
        .phase = phase,
        .callbackFunc = callback,
        .refcon = NULL,
    };

    *flight_loop_id = XPLMCreateFlightLoop(&params);

    XPLMScheduleFlightLoop(*flight_loop_id, CALL_ON_NEXT_FRAME, 1);
}

PLUGIN_API int XPluginStart(char *name, char *sig, char *desc) {
    strcpy(name, "XPRC");
    strcpy(sig, "de.energiequant.xprc");
    strcpy(desc, "XP Remote Control");
    
    if (fatal_error) {
        printf("[XPRC] a fatal error has occurred, XPRC is stuck - simulator restart required\n");
        return 1;
    }
    
    // FIXME: load password from persistence and auto-generate if missing
    server_config.password = "brwSrmyrKNnycC3cEt225NNbJRRaqm74";

    // TODO: load network settings from persistence
    server_config.network.enable_ipv6 = true;
    server_config.network.interface = INTERFACE_LOCAL;
    server_config.network.port = 23042;
    
    if (mtx_init(&task_queue_mutex, mtx_plain) != thrd_success) {
        printf("[XPRC] failed to initialize task queue mutex\n");
        fatal_error = true;
        return 1;
    }
    has_task_queue_mutex = true;
    
    return 1;
}

PLUGIN_API int XPluginEnable() {
    if (fatal_error) {
        printf("[XPRC] a fatal error has occurred, XPRC is stuck - simulator restart required\n");
        return 1;
    }
    
    if (server_started) {
        printf("[XPRC] server is still running; refusing to restart\n");
        return 1;
    }

    if (task_queue_before_flight_model || task_queue_after_flight_model) {
        printf("[XPRC] task queues still exist; refusing to restart\n");
        return 1;
    }

    task_queue_before_flight_model = create_preallocated_list();
    if (!task_queue_before_flight_model) {
        printf("[XPRC] failed to create task queue for 'before flight model' phase\n");
        return 1;
    }
    
    task_queue_after_flight_model = create_preallocated_list();
    if (!task_queue_after_flight_model) {
        printf("[XPRC] failed to create task queue for 'after flight model' phase\n");
        destroy_preallocated_list(task_queue_before_flight_model, NULL, PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS);
        task_queue_before_flight_model = NULL;
        return 1;
    }

    server_config.task_queue_mutex = &task_queue_mutex;
    server_config.task_queue_before_flight_model = task_queue_before_flight_model;
    server_config.task_queue_after_flight_model = task_queue_after_flight_model;
    
    error_t err = start_server(&server, &server_config);
    if (err != ERROR_NONE) {
        printf("[XPRC] failed to start server: %d\n", err);
        destroy_preallocated_list(task_queue_before_flight_model, NULL, PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS);
        destroy_preallocated_list(task_queue_after_flight_model, NULL, PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS);
        task_queue_before_flight_model = NULL;
        task_queue_after_flight_model = NULL;
        return 1;
    }
    server_started = true;

    register_flight_loop(xplm_FlightLoop_Phase_BeforeFlightModel, process_flight_loop_before_flight_model, &flight_loop_before_flight_model_id);
    register_flight_loop(xplm_FlightLoop_Phase_AfterFlightModel,  process_flight_loop_after_flight_model,  &flight_loop_after_flight_model_id);
    flight_loop_registered = true;

    return 1;
}

PLUGIN_API void XPluginDisable() {
    if (fatal_error) {
        printf("[XPRC] a fatal error has occurred, XPRC is stuck - simulator restart required\n");
        return;
    }
    
    if (flight_loop_registered) {
        XPLMDestroyFlightLoop(flight_loop_before_flight_model_id);
        int remaining_cycles = 1000;
        while (locked_task_queue_mutex && remaining_cycles > 0) {
            remaining_cycles--;
            thrd_yield();
        }
        if (locked_task_queue_mutex) {
            printf("[XPRC] task queue cannot be unlocked; plugin shutdown is not possible\n");
            fatal_error = true;
            return;
        }
        XPLMDestroyFlightLoop(flight_loop_after_flight_model_id);
        flight_loop_registered = false;
    }

    if (server_started) {
        error_t err = stop_server(server);
        if (err != ERROR_NONE) {
            printf("[XPRC] server failed to stop: %d\n", err);
            return;
        }
        
        server_started = false;
        server = NULL;
    }

    if (task_queue_before_flight_model) {
        if (task_queue_before_flight_model->size != 0) {
            printf("[XPRC] 'before flight model' task queue is not empty; plugin shutdown is not possible\n");
            fatal_error = true;
            return;
        }
        
        destroy_preallocated_list(task_queue_before_flight_model, NULL, PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS);
        task_queue_before_flight_model = NULL;
    }

    if (task_queue_after_flight_model) {
        if (task_queue_after_flight_model->size != 0) {
            printf("[XPRC] 'after flight model' task queue is not empty; plugin shutdown is not possible\n");
            fatal_error = true;
            return;
        }
        
        destroy_preallocated_list(task_queue_after_flight_model, NULL, PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS);
        task_queue_after_flight_model = NULL;
    }
}

PLUGIN_API void XPluginStop() {
    if (fatal_error) {
        printf("[XPRC] a fatal error has occurred, XPRC is stuck - simulator restart required\n");
        return;
    }
    
    if (has_task_queue_mutex) {
        mtx_destroy(&task_queue_mutex);
        has_task_queue_mutex = false;
    }
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID from, long msg, void *p) {
    // do nothing
}
