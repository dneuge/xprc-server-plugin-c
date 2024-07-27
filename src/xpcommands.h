#ifndef XPCOMMANDS_H
#define XPCOMMANDS_H

/**
 * @file xpcommands.h proxy to abstract hosting X-Plane commands
 *
 * This module provides a proxy that abstracts hosting X-Plane commands. Directly registering commands from XPRC
 * commands is not practical as an X-Plane context may not always be available and the X-Plane command events and
 * life-cycle may conflict with XPRC channel/session handling. Command proxies also simplify handling in regards to
 * X-Plane's command begin/trigger/end events in X-Plane.
 *
 * Similar to DataRef proxies (see dataproxy.h) command proxies are created via #create_xpcommand() and then tracked by
 * a #xpcommand_registry_t. The proxy then needs to be registered to X-Plane via #register_xpcommand() to enter
 * #XPCOMMAND_STATE_REGISTERED. After successful registration, command invocations received through X-Plane will be
 * forwarded to the #xpcommand_callback_f.
 *
 * In case an X-Plane context should be available when the command should be deregistered from X-Plane,
 * #unregister_destroy_xpcommand() could be used but it is generally better to hand the proxy over to the registry
 * for later cleanup using #drop_xpcommand(). Dropping a command proxy (delegating management with the intention to
 * unregister and destroy the proxy) is possible at any time; an X-Plane context is not needed. This is especially
 * helpful when channels/sessions need to be terminated.
 *
 * While a command proxy is #XPCOMMAND_STATE_DROPPED it can still be invoked from X-Plane but invocations will no
 * longer be routed to the #xpcommand_callback_f. #xpcommand_propagation_t will continue to be consistently indicated
 * to X-Plane.
 *
 * Other than for DataRefs, it does not make any sense to reuse and deduplicate command handlers or to look them up
 * from inside XPRC:
 *
 * - X-Plane provides no indication of whether any command handler is attached to a command or not
 * - invocations need to be performed through X-Plane as event routing cannot be determined by XPRC (it can be for
 *   claimed DataRefs)
 *
 * The data structures should not be accessed directly. All functions lock the registry as required, locks do not need
 * to be acquired except if consistency is required over multiple function calls.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef NEED_C11_THREADS_WRAPPER
#include <threads.h>
#else
#include <c11/threads.h>
#endif

#include <XPLMUtilities.h>

typedef struct _xpcommand_registry_t xpcommand_registry_t;

#include "errors.h"
#include "lists.h"

/// the command proxy has been created but the command has not been registered to X-Plane yet
#define XPCOMMAND_STATE_CREATED 0
/// the command proxy has been registered to X-Plane
#define XPCOMMAND_STATE_REGISTERED 1
/// the command proxy has been disconnected and is pending to be unregistered from X-Plane
#define XPCOMMAND_STATE_DROPPED 2
/// the command proxy has been unregistered and destroyed
#define XPCOMMAND_STATE_DESTROYED 3

typedef uint8_t xpcommand_state_t;

/// register the command to be called after X-Plane's own event handling
#define XPCOMMAND_PHASE_AFTER 0
/// register the command to be called before X-Plane's own event handling
#define XPCOMMAND_PHASE_BEFORE 1
/// phase ("inBefore") parameter as specified by XPLMRegisterCommandHandler, directly used for X-Plane API
typedef int xpcommand_phase_t;

/// stop further event processing after this command proxy was called; used to suppress X-Plane's own event handling when combined with #XPCOMMAND_PHASE_BEFORE
#define XPCOMMAND_PROPAGATE_STOP 0
/// continues event processing after this command proxy was called
#define XPCOMMAND_PROPAGATE_CONTINUE 1
/// event propagation options as specified for return values of XPLMCommandCallback_f; directly used for X-Plane API
typedef int xpcommand_propagation_t;

/**
 * Performs the action according to the indicated command phase.
 * @param ref the callback reference as provided to #create_xpcommand() during construction
 * @param xp_phase command phase as defined by X-Plane SDK
 */
typedef void (*xpcommand_callback_f)(void *ref, XPLMCommandPhase xp_phase);

/// XPRC command proxy registry
typedef struct _xpcommand_registry_t {
    /// only cleanup is allowed if set to true
    bool destruction_pending;
    /// synchronizes all access across the registry
    mtx_t mutex;

    /// all command proxies currently handled by the registry
    list_t *commands;
} xpcommand_registry_t;

/// XPRC command proxy
typedef struct {
    /// the registry this proxy is assigned to
    xpcommand_registry_t *registry;
    /// current command state; see XPCOMMAND_STATE_*
    xpcommand_state_t state;

    /// name of the command as it should appear in X-Plane
    char *name;
    /// description of the command as it should appear in X-Plane (submitted to API but may not take effect)
    char *description;
    /// phase to register the command to in X-Plane, see XPCOMMAND_PHASE_*
    xpcommand_phase_t phase;
    /// indicates how X-Plane should continue event handling after invoking this proxy; see XPCOMMAND_PROPAGATE_*
    xpcommand_propagation_t propagate;

    /// the registered command reference as returned and needed by the X-Plane SDK
    XPLMCommandRef xp_ref;

    /// actual command implementation the proxy should forward invocations to
    xpcommand_callback_f callback;
    /// reference for the command implementation
    void *callback_ref;
} xpcommand_t;

/**
 * Creates a new registry for command proxies.
 * @param registry will be set to the created registry instance
 * @return error code; #ERROR_NONE on success
 */
error_t create_xpcommand_registry(xpcommand_registry_t **registry);
/**
 * Destroys the given command proxy registry.
 * @param registry command proxy registry to destroy
 * @return error code; #ERROR_NONE on success
 */
error_t destroy_xpcommand_registry(xpcommand_registry_t *registry);

/**
 * Attempts to lock the command proxy registry, effectively blocking all other command proxy access as well.
 * @param registry command proxy registry to lock
 * @return error code; #ERROR_NONE if lock was acquired
 */
error_t lock_xpcommand_registry(xpcommand_registry_t *registry);
/**
 * Unlocks the command proxy registry; must only be called if a lock is currently being held.
 * @param registry command proxy registry to unlock
 */
void unlock_xpcommand_registry(xpcommand_registry_t *registry);
/**
 * Locks the registry of the given command proxy, effectively blocking all other command proxies as well.
 * @param proxy command proxy whose registry should be locked
 * @return error code; #ERROR_NONE if lock was acquired
 */
error_t lock_xpcommand(xpcommand_t *proxy);
/**
 * Unlocks the registry of the given command proxy; must only be called if a lock is currently being held.
 * @param proxy command proxy whose registry should be unlocked
 */
void unlock_xpcommand(xpcommand_t *proxy);

/**
 * Unregisters all previously dropped commands from X-Plane; intended to be run during maintenance, requires X-Plane
 * context.
 * @param registry command proxy registry to
 * @return error code; #ERROR_NONE on success
 */
error_t unregister_dropped_xpcommands(xpcommand_registry_t *registry);

// used by owners
/**
 * Creates a new command proxy that is not registered to X-Plane yet; can be called without an X-Plane context.
 * @param registry command proxy registry to handle the command
 * @param name command name as it should appear in X-Plane
 * @param description command description as it should appear in X-Plane; may not take effect
 * @param phase phase to register the command to in X-Plane, see XPCOMMAND_PHASE_*
 * @param propagate indicates how X-Plane should continue event handling after invoking this proxy; see XPCOMMAND_PROPAGATE_*
 * @param callback_ref will be passed to the command implementation for context
 * @param callback actual command implementation the proxy should forward invocations to
 * @return the command proxy; NULL on error
 */
xpcommand_t* create_xpcommand(xpcommand_registry_t *registry, char *name, char *description, xpcommand_phase_t phase, xpcommand_propagation_t propagate, void *callback_ref, xpcommand_callback_f callback);
/**
 * Registers the given command proxy to X-Plane; must only be called in an X-Plane context.
 * @param proxy command proxy to register to X-Plane
 * @return error code; #ERROR_NONE on success
 */
error_t register_xpcommand(xpcommand_t *proxy);
/**
 * Unregisters the given command proxy from X-Plane, removes it from the registry and destroys it; must only be called
 * in an X-Plane context.
 * @param proxy command proxy to unregister from X-Plane
 * @return error code; #ERROR_NONE on success
 */
error_t unregister_destroy_xpcommand(xpcommand_t *proxy);
/**
 * Delegates responsibility for deferred command deregistration and proxy destruction to the command proxy; can be
 * called without an X-Plane context; proxy must not be accessed afterwards.
 * @param proxy command proxy to delegate deregistration for
 * @return error code; #ERROR_NONE on success
 */
error_t drop_xpcommand(xpcommand_t *proxy);

#endif
