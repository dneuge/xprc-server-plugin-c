#include <math.h>
#include <stdbool.h>
#include <string.h>

#ifndef NEED_C11_THREADS_WRAPPER
#include <threads.h>
#else
#include <c11/threads.h>
#endif

#include <XPLMDisplay.h>
#include <XPLMPlugin.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>

#include "commands.h"
#include "dataproxy.h"
#include "fileio.h"
#include "lists.h"
#include "logger.h"
#include "server.h"
#include "server_manager.h"
#include "settings_manager.h"
#include "task_schedule.h"
#include "utils.h"
#include "xpcommands.h"
#include "gui/gui.h"

#define CALL_ON_NEXT_FRAME -1.0f
#define SCHEDULE_CLEANING_INTERVAL 500
#define SERVER_MAINTENANCE_INTERVAL 120
#define XPREF_MAINTENANCE_INTERVAL 600

/// minimum path buffer size as documented in XP SDK
#define XP_MINIMUM_PATH_BUFFER_SIZE 512

/// Actual buffer size to use when communicating paths over XP SDK - maximum of actual target platform path lengths and
/// SDK, times 2 to get enough safety margin for possible errors.
#define XP_PATH_BUFFER_SIZE (2 * MAX(XP_MINIMUM_PATH_BUFFER_SIZE, (PATH_MAX_LENGTH+1)))

/// minimum length expected for full X-Plane preferences directory path; used for plausibility check to detect errors in XP SDK usage/malfunction
#define MIN_EXPECTED_XP_PREFERENCES_DIRECTORY_LENGTH 5

XPLMFlightLoopID flight_loop_before_flight_model_id = {0};
XPLMFlightLoopID flight_loop_after_flight_model_id = {0};
bool flight_loop_registered = false;

command_factory_t *command_factory = NULL;

settings_manager_t *settings_manager = NULL;
server_manager_t *server_manager = NULL;

server_config_t server_base_config = {0};

dataproxy_registry_t *dataproxy_registry = NULL;
xpcommand_registry_t *xpcommand_registry = NULL;
xpqueue_t *xpqueue = NULL;

task_schedule_t *task_schedule = NULL;
bool flight_loop_locked_task_schedule = false;

cnd_t post_processing_wait;
thrd_t post_processing_thread;
bool has_post_processing_thread = false;
bool shutdown_post_processing = false;

gui_t *gui = NULL;

bool fatal_error = false;
bool plugin_initialized = false;

int cycles_until_xpref_maintenance = XPREF_MAINTENANCE_INTERVAL;

int run_post_processing_thread(void *arg) {
    error_t err = ERROR_NONE;
    bool has_lock = false;
    
    int cycles_until_schedule_cleaning = SCHEDULE_CLEANING_INTERVAL;
    int cycles_until_server_maintenance = SERVER_MAINTENANCE_INTERVAL;
    
    err = lock_schedule(task_schedule);
    if (err != ERROR_NONE) {
        RCLOG_WARN("post-processing thread failed to lock schedule: %d", err);
        return 0;
    }
    has_lock = true;
    
    while (!shutdown_post_processing) {
        if (cnd_wait(&post_processing_wait, &task_schedule->mutex) != thrd_success) {
            RCLOG_WARN("post-processing thread failed to wait");
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
                RCLOG_WARN("clean_schedule reported error %d", err);
            }
        }

        // TODO: server maintenance should be time-based (once every 2 seconds)
        cycles_until_server_maintenance--;
        if (cycles_until_server_maintenance <= 0) {
            cycles_until_server_maintenance = SERVER_MAINTENANCE_INTERVAL;
            
            unlock_schedule(task_schedule);
            has_lock = false;
            
            err = maintain_server_manager(server_manager);
            if (err != ERROR_NONE) {
                RCLOG_WARN("server manager failed to perform maintenance; error %d", err);
            }

            err = lock_schedule(task_schedule);
            if (err != ERROR_NONE) {
                RCLOG_WARN("failed to regain lock on task schedule after server maintenance: %d", err);
                break;
            }
            has_lock = true;
        }
    }

    if (has_lock) {
        unlock_schedule(task_schedule);
    }

    RCLOG_DEBUG("post-processing thread terminates\n");
    
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
    error_t err = ERROR_NONE;
    
    if (!flight_loop_locked_task_schedule) {
        return CALL_ON_NEXT_FRAME;
    }

    run_tasks(task_schedule, TASK_SCHEDULE_AFTER_FLIGHT_MODEL);
    
    err = flush_xpqueue(xpqueue);
    if (err != ERROR_NONE) {
        // FIXME: if this happens it's unlikely that the issue will fix by itself next iteration; silence warning after first occurrence (in a row) or even mark server as failed?
        RCLOG_WARN("error %d while flushing XP queue", err);
    }

    // TODO: X-Plane reference maintenance should be time-based (once every 2 seconds)
    cycles_until_xpref_maintenance--;
    if (cycles_until_xpref_maintenance <= 0) {
        cycles_until_xpref_maintenance = XPREF_MAINTENANCE_INTERVAL;

        err = unregister_dropped_dataproxies(dataproxy_registry);
        if (err != ERROR_NONE) {
            RCLOG_WARN("error %d while unregistering dropped dataproxies (maintenance)", err);
        }

        err = unregister_dropped_xpcommands(xpcommand_registry);
        if (err != ERROR_NONE) {
            RCLOG_WARN("error %d while unregistering dropped XP commands (maintenance)", err);
        }
    }

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

/**
 * Retrieves X-Plane preferences directory path via the SDK; expensive to call, cache result if needed.
 * @return X-Plane preference directory path, null-terminated without trailing directory separator; NULL on error
 */
static char* get_xp_preferences_directory() {
    char *buffer = zalloc(XP_PATH_BUFFER_SIZE);
    if (!buffer) {
        RCLOG_WARN("failed to allocate XP path buffer (%d bytes)", XP_PATH_BUFFER_SIZE);
        return NULL;
    }

    strcpy(buffer, "dummy.txt"); // TODO: this may not be necessary; does XPLMGetPrefsPath not expand but return _some_ file path?
    XPLMGetPrefsPath(buffer);
    size_t dummy_filepath_length = strlen(buffer);
    if (dummy_filepath_length >= XP_PATH_BUFFER_SIZE) {
        RCLOG_ERROR("buffer overrun detected via XPLMGetPrefsPath in get_xp_preferences_directory; fatal_error (%ld >= %d)", dummy_filepath_length, XP_PATH_BUFFER_SIZE);
        goto fatal_error;
    }

    XPLMExtractFileAndPath(buffer); // turns last directory separator into null-termination
    size_t directory_length = strlen(buffer);
    if (directory_length >= XP_PATH_BUFFER_SIZE) {
        RCLOG_ERROR("buffer overrun detected via XPLMExtractFileAndPath in get_xp_preferences_directory; fatal_error (%ld >= %d)", directory_length, XP_PATH_BUFFER_SIZE);
        goto fatal_error;
    }

    if (directory_length >= dummy_filepath_length) {
        RCLOG_ERROR("expected path after XPLMExtractFileAndPath (%ld) to be shorter than XPLMGetPrefsPath (%ld); fatal_error", directory_length, dummy_filepath_length);
        goto fatal_error;
    }

    if (directory_length < MIN_EXPECTED_XP_PREFERENCES_DIRECTORY_LENGTH) {
        RCLOG_ERROR("X-Plane preference path is implausible (got %ld characters, expected at least %d); fatal_error", directory_length, MIN_EXPECTED_XP_PREFERENCES_DIRECTORY_LENGTH);
        goto fatal_error;
    }

    if (buffer[directory_length-1] == DIRECTORY_SEPARATOR) {
        RCLOG_WARN("Possible SDK change: X-Plane preference path ends in directory separator (will try to shorten and continue)");
        directory_length--;

        if (buffer[directory_length-1] == DIRECTORY_SEPARATOR) {
            RCLOG_ERROR("X-Plane preference path still ends in directory separator after shortening; fatal_error");
            goto fatal_error;
        }
    }

    // only keep the directory part that we actually need, free the (much larger) original buffer
    // directory_length may be shorter than strlen; see tolerant SDK handling above
    char *out = copy_partial_string(buffer, directory_length);
    if (!out) {
        RCLOG_WARN("failed to allocate preference path output string");
    }

    free(buffer);

    return out;

fatal_error:
    fatal_error = 1;
    free(buffer);
    return NULL;
}

PLUGIN_API int XPluginStart(char *name, char *sig, char *desc) {
    strcpy(name, "XPRC");
    strcpy(sig, "de.energiequant.xprc");
    strcpy(desc, "XP Remote Control");

    xprc_log_init();

    // TODO: make log levels configurable
    xprc_set_min_log_level_console(RCLOG_LEVEL_TRACE);
    xprc_set_min_log_level_xplane(RCLOG_LEVEL_INFO);

    if (plugin_initialized) {
        RCLOG_ERROR("plugin has already been initialized without shutdown (did you install it twice?) - simulator restart required");
        fatal_error = 1;
        return 1;
    }
    
    if (fatal_error) {
        RCLOG_ERROR("a fatal error has occurred, XPRC is stuck - simulator restart required");
        return 1;
    }

    // check that plugin and X-Plane use the same directory separator
    // TODO: Macs will probably need XPLM_USE_NATIVE_PATHS to be set for UNIX-style paths (or XPRC needs to use pre-10 colons), see X-Plane documentation, check low-level stdio.h behaviour; must not be set on Windows
    const char *xp_directory_separator = XPLMGetDirectorySeparator();
    if (!xp_directory_separator) {
        RCLOG_WARN("X-Plane did not return any directory separator, consistency cannot be checked");
    } else if (strlen(xp_directory_separator) != 1) {
        RCLOG_WARN("X-Plane directory separator has an unexpected length: got %ld, expected 1", strlen(xp_directory_separator));
    } else if (DIRECTORY_SEPARATOR != xp_directory_separator[0]) {
        RCLOG_ERROR("X-Plane directory separator is different from plugin platform code! Plugin is incompatible and will refuse to start; please report to XPRC developers. XP: \"%s\", XPRC: '%c'", xp_directory_separator, DIRECTORY_SEPARATOR);
        fatal_error = 1;
        return 1;
    }

    if (cnd_init(&post_processing_wait) != thrd_success) {
        RCLOG_ERROR("failed to create condition for post-processing thread - simulator restart required");
        fatal_error = 1;
        return 1;
    }

    command_factory = create_command_factory();
    if (!command_factory) {
        RCLOG_ERROR("failed to create command factory - simulator restart required");
        fatal_error = 1;
        return 1;
    }

    server_base_config.command_factory = command_factory;

    plugin_initialized = true;
    
    return 1;
}

PLUGIN_API int XPluginEnable() {
    RCLOG_INFO("Enabling plugin...");

    error_t err = ERROR_NONE;
    
    if (fatal_error) {
        RCLOG_ERROR("a fatal error has occurred, XPRC is stuck - simulator restart required");
        return 1;
    }

    if (server_manager) {
        RCLOG_ERROR("server manager already/still exist; did you install the plugin twice? simulator restart required");
        fatal_error = true;
        return 1;
    }

    if (gui) {
        RCLOG_ERROR("GUI is still registered; did you install the plugin twice? simulator restart required");
        fatal_error = true;
        return 1;
    }

    if (task_schedule) {
        RCLOG_ERROR("task schedule already exists; did you install the plugin twice? simulator restart required");
        fatal_error = true;
        return 1;
    }
    
    if (xpqueue) {
        RCLOG_ERROR("XP queue already exists; did you install the plugin twice? simulator restart required");
        fatal_error = true;
        return 1;
    }

    if (settings_manager) {
        RCLOG_ERROR("settings manager already/still exist; did you install the plugin twice? simulator restart required");
        fatal_error = true;
        return 1;
    }

    char *preferences_directory = get_xp_preferences_directory();
    if (!preferences_directory) {
        RCLOG_ERROR("failed to get preferences directory from X-Plane; simulator restart required");
        fatal_error = true;
        return 1;
    }

    settings_manager = create_settings_manager(preferences_directory);
    free(preferences_directory);
    preferences_directory = NULL;

    if (!settings_manager) {
        RCLOG_ERROR("failed to create settings manager; plugin cannot run without configuration");
        return 1;
    }

    RCLOG_DEBUG("Loading settings...");
    err = configure_settings_manager_from_storage(settings_manager);
    if (err != ERROR_NONE) {
        // TODO: inform user via popup according to error code
        RCLOG_WARN("configure_settings_manager_from_storage returned %d", err);
    }

    server_manager = create_server_manager(settings_manager);
    if (!server_manager) {
        RCLOG_ERROR("failed to create server manager; plugin cannot run");
        return 1;
    }

    gui = gui_create(settings_manager);
    if (!gui) {
        RCLOG_ERROR("failed to initialize GUI - simulator restart required");
        fatal_error = true;
        return 1;
    }

    err = create_xpqueue(&xpqueue);
    if (err != ERROR_NONE) {
        RCLOG_ERROR("failed to create XP queue: %d", err);
        return 1;
    }
    server_base_config.xpqueue = xpqueue;

    if (dataproxy_registry) {
        RCLOG_ERROR("dataproxy registry already exists; did you install the plugin twice? simulator restart required");
        fatal_error = true;
        return 1;
    }

    if (xpcommand_registry) {
        RCLOG_ERROR("XP command registry already exists; did you install the plugin twice? simulator restart required");
        fatal_error = true;
        return 1;
    }

    err = create_dataproxy_registry(&dataproxy_registry);
    if (err != ERROR_NONE) {
        RCLOG_ERROR("failed to create dataproxy registry: %d", err);
        return 1;
    }
    server_base_config.dataproxy_registry = dataproxy_registry;

    err = create_xpcommand_registry(&xpcommand_registry);
    if (err != ERROR_NONE) {
        RCLOG_ERROR("failed to create XP command registry: %d", err);
        return 1;
    }
    server_base_config.xpcommand_registry = xpcommand_registry;

    cycles_until_xpref_maintenance = XPREF_MAINTENANCE_INTERVAL;
    
    err = create_task_schedule(&task_schedule);
    if (err != ERROR_NONE) {
        RCLOG_ERROR("failed to create task schedule: %d", err);
        return 1;
    }
    server_base_config.task_schedule = task_schedule;

    if (has_post_processing_thread) {
        RCLOG_ERROR("post-processing thread still exists; simulator restart required");
        fatal_error = true;
        return 1;
    }
    
    shutdown_post_processing = false;
    if (thrd_create(&post_processing_thread, run_post_processing_thread, NULL) != thrd_success) {
        RCLOG_ERROR("failed to spawn post-processing thread");
        return 1;
    }
    has_post_processing_thread = true;

    err = provide_managed_server_base_config(server_manager, &server_base_config);
    if (err != ERROR_NONE) {
        RCLOG_ERROR("failed to provide base server configuration to manager");
        return 1;
    }

    err = start_managed_server(server_manager);
    if (err != ERROR_NONE) {
        RCLOG_WARN("failed to start server: %d", err);
        // FIXME: post-processing thread depends on task_schedule and has to be terminated first
        destroy_task_schedule(task_schedule);
        task_schedule = NULL;
        return 1;
    }

    register_flight_loop(xplm_FlightLoop_Phase_BeforeFlightModel, process_flight_loop_before_flight_model, &flight_loop_before_flight_model_id);
    register_flight_loop(xplm_FlightLoop_Phase_AfterFlightModel,  process_flight_loop_after_flight_model,  &flight_loop_after_flight_model_id);
    flight_loop_registered = true;

    return 1;
}

PLUGIN_API void XPluginDisable() {
    error_t err = ERROR_NONE;
    
    if (fatal_error) {
        RCLOG_ERROR("a fatal error has occurred, XPRC is stuck - simulator restart required");
        return;
    }

    gui_destroy(gui);
    gui = NULL;

    if (server_manager) {
        err = shutdown_managed_server(server_manager);
        if (err != ERROR_NONE) {
            RCLOG_ERROR("failed to shut down server: %d", err);
            fatal_error = true;
            return;
        }

        err = destroy_server_manager(server_manager);
        if (err != ERROR_NONE) {
            RCLOG_ERROR("failed to destroy server manager: %d", err);
            fatal_error = true;
            return;
        }
        server_manager = NULL;
    }

    bool can_join_post_processing_thread = true;
    if (has_post_processing_thread) {
        bool schedule_locked = false;
        if (task_schedule && lock_schedule(task_schedule) == ERROR_NONE) {
            schedule_locked = true;
        } else {
            RCLOG_ERROR("failed to lock task schedule to signal shutdown to post-processing thread");
            can_join_post_processing_thread = false;
        }
    
        shutdown_post_processing = true;
        if (cnd_broadcast(&post_processing_wait) != thrd_success) {
            RCLOG_ERROR("post processing thread cannot be notified");
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
            RCLOG_ERROR("task schedule cannot be unlocked; plugin shutdown is not possible");
            fatal_error = true;
            return;
        }
        XPLMDestroyFlightLoop(flight_loop_after_flight_model_id);
        flight_loop_registered = false;
    }

    if (has_post_processing_thread) {
        if (!can_join_post_processing_thread) {
            RCLOG_ERROR("post processing thread cannot be joined safely - simulator restart required");
            fatal_error = true;
            return;
        } else {
            if (thrd_join(post_processing_thread, NULL) != thrd_success) {
                RCLOG_ERROR("post processing thread cannot be joined; plugin shutdown is not possible");
                fatal_error = true;
                return;
            }
            
            has_post_processing_thread = false;
        }
    }

    if (task_schedule) {
        err = destroy_task_schedule(task_schedule);
        if (err != ERROR_NONE) {
            RCLOG_ERROR("task schedule could not be destroyed (error %d); plugin shutdown is not possible", err);
            fatal_error = true;
            return;
        }
        task_schedule = NULL;
    }

    if (xpcommand_registry) {
        err = unregister_dropped_xpcommands(xpcommand_registry);
        if (err != ERROR_NONE) {
            RCLOG_ERROR("dropped XP commands could not be unregistered (error %d); plugin shutdown is not possible", err);
            fatal_error = true;
            return;
        }
        
        err = destroy_xpcommand_registry(xpcommand_registry);
        if (err != ERROR_NONE) {
            RCLOG_ERROR("XP command registry could not be destroyed (error %d); plugin shutdown is not possible", err);
            fatal_error = true;
            return;
        }
        xpcommand_registry = NULL;
    }

    if (dataproxy_registry) {
        err = unregister_dropped_dataproxies(dataproxy_registry);
        if (err != ERROR_NONE) {
            RCLOG_ERROR("dropped dataproxies could not be unregistered (error %d); plugin shutdown is not possible", err);
            fatal_error = true;
            return;
        }
        
        err = destroy_dataproxy_registry(dataproxy_registry);
        if (err != ERROR_NONE) {
            RCLOG_ERROR("dataproxy registry could not be destroyed (error %d); plugin shutdown is not possible", err);
            fatal_error = true;
            return;
        }
        dataproxy_registry = NULL;
    }

    if (xpqueue) {
        err = flush_xpqueue(xpqueue);
        if (err != ERROR_NONE) {
            RCLOG_ERROR("failed final flush of XP queue (error %d); plugin shutdown is not possible", err);
            fatal_error = true;
            return;
        }

        err = destroy_xpqueue(xpqueue);
        if (err != ERROR_NONE) {
            RCLOG_ERROR("XP queue could not be destroyed (error %d); plugin shutdown is not possible", err);
            fatal_error = true;
            return;
        }
        xpqueue = NULL;
    }

    if (settings_manager) {
        err = destroy_settings_manager(settings_manager);
        if (err != ERROR_NONE) {
            RCLOG_ERROR("settings manager could not be destroyed (error %d); plugin shutdown is not possible", err);
            fatal_error = true;
            return;
        }
        settings_manager = NULL;
    }
}

PLUGIN_API void XPluginStop() {
    if (fatal_error) {
        RCLOG_ERROR("a fatal error has occurred, XPRC is stuck - simulator restart required");
        return;
    }

    cnd_destroy(&post_processing_wait);

    if (command_factory) {
        destroy_command_factory(command_factory);
        command_factory = NULL;
    }

    xprc_log_destroy();
    plugin_initialized = false;
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID from, long msg, void *p) {
    // do nothing
}
