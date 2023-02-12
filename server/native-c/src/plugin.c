#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <XPLMPlugin.h>
#include <XPLMProcessing.h>
#include <XPLMUtilities.h>

#define CALL_ON_NEXT_FRAME -1.0f

XPLMFlightLoopID flight_loop_before_flight_model_id = {0};
XPLMFlightLoopID flight_loop_after_flight_model_id = {0};
bool flight_loop_registered = false;

long long int num_cycles_before = 0;
long long int num_cycles_after = 0;

static float process_flight_loop_before_flight_model(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon) {
    num_cycles_before++;
    return CALL_ON_NEXT_FRAME;
}

static float process_flight_loop_after_flight_model(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon) {
    num_cycles_after++;
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
    num_cycles_before = 0;
    num_cycles_after = 0;
    
    register_flight_loop(xplm_FlightLoop_Phase_BeforeFlightModel, process_flight_loop_before_flight_model, &flight_loop_before_flight_model_id);
    register_flight_loop(xplm_FlightLoop_Phase_AfterFlightModel,  process_flight_loop_after_flight_model,  &flight_loop_after_flight_model_id);
    flight_loop_registered = true;
    return 1;
}

PLUGIN_API void XPluginDisable() {
    XPLMDestroyFlightLoop(flight_loop_before_flight_model_id);
    XPLMDestroyFlightLoop(flight_loop_after_flight_model_id);
    flight_loop_registered = false;

    printf("[XPRC] num calls: %lld %lld\n", num_cycles_before, num_cycles_after);
}

PLUGIN_API void XPluginStop() {
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID from, long msg, void *p) {
    // do nothing
}
