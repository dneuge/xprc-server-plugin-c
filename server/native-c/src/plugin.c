#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <XPLMPlugin.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>

#include "commands.h"
#include "lists.h"
#include "server.h"
#include "task_schedule.h"

#define CALL_ON_NEXT_FRAME -1.0f
#define SCHEDULE_CLEANING_INTERVAL 500
#define SERVER_MAINTENANCE_INTERVAL 120

XPLMFlightLoopID flight_loop_before_flight_model_id = {0};
XPLMFlightLoopID flight_loop_after_flight_model_id = {0};
bool flight_loop_registered = false;

command_factory_t *command_factory = NULL;

server_t *server = NULL;
server_config_t server_config = {0};
bool server_started = false;

task_schedule_t *task_schedule = NULL;
bool flight_loop_locked_task_schedule = false;

cnd_t post_processing_wait;
thrd_t post_processing_thread;
bool has_post_processing_thread = false;
bool shutdown_post_processing = false;

bool fatal_error = false;
bool plugin_initialized = false;

int run_post_processing_thread(void *arg) {
    error_t err = ERROR_NONE;
    bool has_lock = false;
    
    int cycles_until_schedule_cleaning = SCHEDULE_CLEANING_INTERVAL;
    int cycles_until_server_maintenance = SERVER_MAINTENANCE_INTERVAL;
    
    err = lock_schedule(task_schedule);
    if (err != ERROR_NONE) {
        printf("[XPRC] post-processing thread failed to lock schedule: %d\n", err);
        return 0;
    }
    has_lock = true;
    
    while (!shutdown_post_processing) {
        if (cnd_wait(&post_processing_wait, &task_schedule->mutex) != thrd_success) {
            printf("[XPRC] post-processing thread failed to wait\n");
            break;
        }

        if (shutdown_post_processing) {
            break;
        }

        run_tasks(task_schedule, TASK_SCHEDULE_POST_PROCESSING);
        
        cycles_until_schedule_cleaning--;
        if (cycles_until_schedule_cleaning <= 0) {
            cycles_until_schedule_cleaning = SCHEDULE_CLEANING_INTERVAL;
            err = clean_schedule(task_schedule);
            if (err != ERROR_NONE) {
                printf("[XPRC] clean_schedule reported error %d\n", err);
            }
        }

        // TODO: server maintenance should be time-based (once every 2 seconds)
        cycles_until_server_maintenance--;
        if (cycles_until_server_maintenance <= 0) {
            cycles_until_server_maintenance = SERVER_MAINTENANCE_INTERVAL;
            
            unlock_schedule(task_schedule);
            has_lock = false;
            
            err = maintain_server(server);
            if (err != ERROR_NONE) {
                printf("[XPRC] server maintenance reported error %d\n", err);
            }
            
            err = lock_schedule(task_schedule);
            if (err != ERROR_NONE) {
                printf("[XPRC] failed to regain lock on task schedule after server maintenance: %d\n", err);
                break;
            }
            has_lock = true;
        }
    }

    if (has_lock) {
        unlock_schedule(task_schedule);
    }

    printf("[XPRC] post-processing thread terminates\n");
    
    return 0;
}

static float process_flight_loop_before_flight_model(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon) {
    if (lock_schedule(task_schedule)) {
        // TODO: is there any way we could log that?
        return CALL_ON_NEXT_FRAME;
    }

    flight_loop_locked_task_schedule = true;

    run_tasks(task_schedule, TASK_SCHEDULE_BEFORE_FLIGHT_MODEL);
    
    return CALL_ON_NEXT_FRAME;
}

static float process_flight_loop_after_flight_model(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon) {
    if (!flight_loop_locked_task_schedule) {
        return CALL_ON_NEXT_FRAME;
    }

    run_tasks(task_schedule, TASK_SCHEDULE_AFTER_FLIGHT_MODEL);

    cnd_broadcast(&post_processing_wait);
    
    flight_loop_locked_task_schedule = false;
    unlock_schedule(task_schedule);
    
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

    if (plugin_initialized) {
        printf("[XPRC] plugin has already been initialized without shutdown (did you install it twice?) - simulator restart required\n");
        fatal_error = 1;
        return 1;
    }
    
    if (fatal_error) {
        printf("[XPRC] a fatal error has occurred, XPRC is stuck - simulator restart required\n");
        return 1;
    }

    if (cnd_init(&post_processing_wait) != thrd_success) {
        printf("[XPRC] failed to create condition for post-processing thread - simulator restart required\n");
        fatal_error = 1;
        return 1;
    }

    command_factory = create_command_factory();
    if (!command_factory) {
        printf("[XPRC] failed to create command factory - simulator restart required\n");
        fatal_error = 1;
        return 1;
    }
    
    // FIXME: load password from persistence and auto-generate if missing
    server_config.password = "brwSrmyrKNnycC3cEt225NNbJRRaqm74";

    // TODO: load network settings from persistence
    server_config.network.enable_ipv6 = true;
    server_config.network.interface = INTERFACE_LOCAL;
    server_config.network.port = 23042;

    server_config.command_factory = command_factory;

    plugin_initialized = true;
    
    return 1;
}

PLUGIN_API int XPluginEnable() {
    error_t err = ERROR_NONE;
    
    if (fatal_error) {
        printf("[XPRC] a fatal error has occurred, XPRC is stuck - simulator restart required\n");
        return 1;
    }
    
    if (server_started) {
        printf("[XPRC] server is already/still running; did you install the plugin twice? simulator restart required\n");
        fatal_error = true;
        return 1;
    }

    if (task_schedule) {
        printf("[XPRC] task schedule already exists; did you install the plugin twice? simulator restart required\n");
        fatal_error = true;
        return 1;
    }
    
    err = create_task_schedule(&task_schedule);
    if (err != ERROR_NONE) {
        printf("[XPRC] failed to create task schedule\n");
        return 1;
    }
    server_config.task_schedule = task_schedule;

    if (has_post_processing_thread) {
        printf("[XPRC] post-processing thread still exists; simulator restart required\n");
        fatal_error = true;
        return 1;
    }
    
    shutdown_post_processing = false;
    if (thrd_create(&post_processing_thread, run_post_processing_thread, NULL) != thrd_success) {
        printf("[XPRC] failed to spawn post-processing thread\n");
        return 1;
    }
    has_post_processing_thread = true;
    
    err = start_server(&server, &server_config);
    if (err != ERROR_NONE) {
        printf("[XPRC] failed to start server: %d\n", err);
        destroy_task_schedule(task_schedule);
        task_schedule = NULL;
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

    if (server_started) {
        error_t err = stop_server(server);
        if (err != ERROR_NONE) {
            printf("[XPRC] server failed to stop: %d\n", err);
            fatal_error = true;
            return;
        }
        
        server_started = false;
        server = NULL;
    }

    bool can_join_post_processing_thread = true;
    if (has_post_processing_thread) {
        bool schedule_locked = false;
        if (task_schedule && lock_schedule(task_schedule) == ERROR_NONE) {
            schedule_locked = true;
        } else {
            printf("[XPRC] failed to lock task schedule to signal shutdown to post-processing thread\n");
            can_join_post_processing_thread = false;
        }
    
        shutdown_post_processing = true;
        if (cnd_broadcast(&post_processing_wait) != thrd_success) {
            printf("[XPRC] post processing thread cannot be notified\n");
            can_join_post_processing_thread = false;
        }

        if (schedule_locked) {
            unlock_schedule(task_schedule);
            schedule_locked = false;
        }
    }
    
    if (flight_loop_registered) {
        XPLMDestroyFlightLoop(flight_loop_before_flight_model_id);
        int remaining_cycles = 1000;
        while (flight_loop_locked_task_schedule && remaining_cycles > 0) {
            remaining_cycles--;
            thrd_yield();
        }
        if (flight_loop_locked_task_schedule) {
            printf("[XPRC] task schedule cannot be unlocked; plugin shutdown is not possible\n");
            fatal_error = true;
            return;
        }
        XPLMDestroyFlightLoop(flight_loop_after_flight_model_id);
        flight_loop_registered = false;
    }

    if (has_post_processing_thread) {
        if (!can_join_post_processing_thread) {
            printf("[XPRC] post processing thread cannot be joined safely - simulator restart required\n");
            fatal_error = true;
            return;
        } else {
            if (thrd_join(post_processing_thread, NULL) != thrd_success) {
                printf("[XPRC] post processing thread cannot be joined; plugin shutdown is not possible\n");
                fatal_error = true;
                return;
            }
            
            has_post_processing_thread = false;
        }
    }

    if (task_schedule) {
        error_t err = destroy_task_schedule(task_schedule);
        if (err != ERROR_NONE) {
            printf("[XPRC] task schedule could not be destroyed (error %d); plugin shutdown is not possible\n", err);
            fatal_error = true;
            return;
        }
        task_schedule = NULL;
    }
}

PLUGIN_API void XPluginStop() {
    if (fatal_error) {
        printf("[XPRC] a fatal error has occurred, XPRC is stuck - simulator restart required\n");
        return;
    }

    cnd_destroy(&post_processing_wait);

    if (command_factory) {
        destroy_command_factory(command_factory);
        command_factory = NULL;
    }

    plugin_initialized = false;
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID from, long msg, void *p) {
    // do nothing
}
