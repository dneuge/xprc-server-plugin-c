#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <XPLMPlugin.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>

#define CALL_ON_NEXT_FRAME -1.0f

XPLMFlightLoopID flight_loop_before_flight_model_id = {0};
XPLMFlightLoopID flight_loop_after_flight_model_id = {0};
bool flight_loop_registered = false;

#define DIFF_CYCLES 120
struct timespec last_called_before = {0};
struct timespec last_called_after = {0};
double micros_between_total_cycles[DIFF_CYCLES] = {0};
double micros_between_before_and_after[DIFF_CYCLES] = {0};
int calls_before = 0;
int calls_after = 0;

static double micros_between(struct timespec *earlier, struct timespec *later) {
    long long int earlier_micros = (earlier->tv_sec * 1000000) + (earlier->tv_nsec / 1000);
    long long int later_micros = (later->tv_sec * 1000000) + (later->tv_nsec / 1000);
    
    return (double) (later_micros - earlier_micros);
}

static float process_flight_loop_before_flight_model(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon) {
    struct timespec now = {0};
    timespec_get(&now, TIME_UTC);
    double micros_since_last_cycle = micros_between(&last_called_after, &now);
    last_called_before = now;

    micros_between_total_cycles[calls_before++] = micros_since_last_cycle;
    if (calls_before >= DIFF_CYCLES) {
        double sum = 0.0;
        double min = INFINITY;
        double max = 0.0;
        for (int i=0; i<DIFF_CYCLES; i++) {
            double value = micros_between_total_cycles[i];
            if (value < min) {
                min = value;
            }
            if (value > max) {
                max = value;
            }
            sum += value;
        }
        double avg = sum / DIFF_CYCLES;
        printf("[XPRC] call time between total cycles: avg %.0f (min %.0f / max %.0f)\n", avg, min, max);
        calls_before = 0;
    }
    
    return CALL_ON_NEXT_FRAME;
}

static float process_flight_loop_after_flight_model(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon) {
    struct timespec now = {0};
    timespec_get(&now, TIME_UTC);
    double micros_since_before = micros_between(&last_called_before, &now);
    last_called_after = now;

    micros_between_before_and_after[calls_after++] = micros_since_before;
    if (calls_after >= DIFF_CYCLES) {
        double sum = 0.0;
        double min = INFINITY;
        double max = 0.0;
        for (int i=0; i<DIFF_CYCLES; i++) {
            double value = micros_between_before_and_after[i];
            if (value < min) {
                min = value;
            }
            if (value > max) {
                max = value;
            }
            sum += value;
        }
        double avg = sum / DIFF_CYCLES;
        printf("[XPRC] call time between before and after: avg %.0f (min %.0f / max %.0f)\n", avg, min, max);
        calls_after = 0;
    }
    
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
    
    return 1;
}

PLUGIN_API int XPluginEnable() {
    timespec_get(&last_called_before, TIME_UTC);
    timespec_get(&last_called_after, TIME_UTC);
    
    register_flight_loop(xplm_FlightLoop_Phase_BeforeFlightModel, process_flight_loop_before_flight_model, &flight_loop_before_flight_model_id);
    register_flight_loop(xplm_FlightLoop_Phase_AfterFlightModel,  process_flight_loop_after_flight_model,  &flight_loop_after_flight_model_id);
    flight_loop_registered = true;
    return 1;
}

PLUGIN_API void XPluginDisable() {
    XPLMDestroyFlightLoop(flight_loop_before_flight_model_id);
    XPLMDestroyFlightLoop(flight_loop_after_flight_model_id);
    flight_loop_registered = false;
}

PLUGIN_API void XPluginStop() {
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID from, long msg, void *p) {
    // do nothing
}
