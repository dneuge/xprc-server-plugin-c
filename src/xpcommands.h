#ifndef XPCOMMANDS_H
#define XPCOMMANDS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <threads.h>

#include <XPLMUtilities.h>

typedef struct _xpcommand_registry_t xpcommand_registry_t;

#include "errors.h"
#include "lists.h"

#define XPCOMMAND_NO_REF NULL

#define XPCOMMAND_STATE_CREATED 0
#define XPCOMMAND_STATE_REGISTERED 1
#define XPCOMMAND_STATE_DROPPED 2
#define XPCOMMAND_STATE_DESTROYED 3

typedef uint8_t xpcommand_state_t;

#define XPCOMMAND_PHASE_AFTER 0
#define XPCOMMAND_PHASE_BEFORE 1
typedef int xpcommand_phase_t; // directly used on XP API

#define XPCOMMAND_PROPAGATE_STOP 0
#define XPCOMMAND_PROPAGATE_CONTINUE 1
typedef int xpcommand_propagation_t; // directly used on XP API

typedef void (*xpcommand_callback_f)(void *ref, XPLMCommandPhase xp_phase);

/* Command proxies are similar to DataRef proxies only in so far
 * as they need to abstract event forwarding due to threads not being
 * able to asynchronously unregister from X-Plane. XP may continue
 * calling already dropped handlers as a result.
 * Other than for DataRefs, it does not make any sense to reuse and
 * deduplicate command handlers:
 *
 * - X-Plane provides no indication of whether any command handler
 *   is attached to a command or not
 *
 * - invocations need to be performed through X-Plane as event routing
 *   cannot be determined by XPRC (it can be for claimed DataRefs)
 */

typedef struct _xpcommand_registry_t {
    bool destruction_pending;
    mtx_t mutex;

    list_t *commands;
} xpcommand_registry_t;

typedef struct {
    xpcommand_registry_t *registry;
    xpcommand_state_t state;
    
    char *name;
    char *description;
    xpcommand_phase_t phase;
    xpcommand_propagation_t propagate;
    
    XPLMCommandRef xp_ref;

    xpcommand_callback_f callback;
    void *callback_ref;
} xpcommand_t;

error_t create_xpcommand_registry(xpcommand_registry_t **registry);
error_t destroy_xpcommand_registry(xpcommand_registry_t *registry);

error_t lock_xpcommand_registry(xpcommand_registry_t *registry);
void unlock_xpcommand_registry(xpcommand_registry_t *registry);
error_t lock_xpcommand(xpcommand_t *proxy);
void unlock_xpcommand(xpcommand_t *proxy);

error_t unregister_dropped_xpcommands(xpcommand_registry_t *registry); // run only in XP context

// used by owners
xpcommand_t* create_xpcommand(xpcommand_registry_t *registry, char *name, char *description, xpcommand_phase_t phase, xpcommand_propagation_t propagate, void *callback_ref, xpcommand_callback_f callback);
error_t register_xpcommand(xpcommand_t *proxy); // run only in XP context
error_t unregister_destroy_xpcommand(xpcommand_t *proxy); // run only in XP context
error_t drop_xpcommand(xpcommand_t *proxy); // queues the command for delayed unregistration (XP context not needed)

#endif
