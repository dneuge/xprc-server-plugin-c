#ifndef DATAPROXY_H
#define DATAPROXY_H

/**
 * @file dataproxy.h
 * Data proxies abstract and decouple hosting of datarefs between XPRC and X-Plane for asynchronous handling.
 *
 * Instead of directly registering datarefs in X-Plane all commands in XPRC should register a #dataproxy_t.
 * Some of the benefits are:
 *
 * - proxied datarefs can be looked up through the #dataproxy_registry_t
 * - when using deferred deregistration via #drop_dataproxy() only #register_dataproxy() needs to be called within an
 *   X-Plane context, all other functions can be called from any thread
 * - permissions (#dataproxy_permission_t) can be configured to let the proxy already filter out unwanted
 *   write requests from X-Plane or foreign sessions
 * - callback functions (#dataproxy_operations_t) are more convenient to use than the X-Plane SDK originals, the only
 *   distinction to be made is between simple (xplmType_Int/Float/Double) or array (xplmType_IntArray/FloatArray/Data)
 *   access
 *
 * A single application-wide #dataproxy_registry_t is used to keep track of all datarefs currently being handled.
 *
 * Instances of #dataproxy_t have the following life-cycle, recorded as #dataproxy_state_t:
 *
 * 1. [INACTIVE => RESERVED] The proxy first needs to be defined and reserved via #reserve_dataproxy(). This may reuse
 *    and/or redefine an earlier proxy. The returned #dataproxy_t is an exclusive lease for the specified dataref;
 *    further requests to reserve the same dataref within XPRC will fail until the previous reservation is released.
 * 2. [RESERVED => REGISTERED] When the handler is ready to be accessed, the proxy needs to be registered to the
 *    simulator within an X-Plane context (thread/callback) by calling #register_dataproxy(). Doing so also opens it for
 *    requests sent internally within XPRC.
 * 3. [REGISTERED => RESERVED] #unregister_dataproxy() is called to remove the registration from the simulator again
 *    while the caller holds an X-Plane context (thread/callback). This also prevents further request routing within
 *    XPRC.
 * 4. [RESERVED => INACTIVE] The still reserved dataproxy needs to be released when it is no longer needed after
 *    deregistration using #release_dataproxy(). Doing so allows the dataref to be reserved by other callers again.
 * 5. [RESERVED/REGISTERED => DROPPED => INACTIVE] To quickly abort a command or to avoid having to wait for an X-Plane
 *    context to call #unregister_dataproxy(), the data proxy ownership can be immediately dropped via
 *    #drop_dataproxy(). Unregistering it from X-Plane (if needed) before eventually releasing the proxy will happen
 *    at a later time in a deferred call to #unregister_dropped_dataproxies() by plugin maintenance while holding an
 *    X-Plane context.
 *
 * All functions working on #dataproxy_t acquire a lock to #dataproxy_registry_t on their own. However, when performing
 * several dependant operations in a row, it can be useful to explicitly #lock_dataproxy_registry() and
 * #unlock_dataproxy_registry() to ensure a consistent result.
 *
 * While management functions such as #reserve_dataproxy(), #release_dataproxy(), #register_dataproxy(),
 * #unregister_dataproxy() and #drop_dataproxy() must only be used by owners of a dataref, all functions for lookup,
 * such as #find_registered_dataproxy() and #list_dataproxies_with_state(), and data requests (#dataproxy_get_name(),
 * #dataproxy_get_types(), #dataproxy_can_write(), #dataproxy_simple_get(), #dataproxy_simple_set(),
 * #dataproxy_array_get(), #dataproxy_array_length(), #dataproxy_array_update()) are generally available within XPRC.
 * The benefit of using XPRC-internal proxy functions, when possible, is that they can be accessed at any time without
 * requiring an X-Plane context or going through X-Plane dataref indirections if the dataref is hosted by XPRC.
 *
 * Note that data proxy backends, just as X-Plane datarefs, can host different data for different data types, even
 * including different array lengths.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef NEED_C11_THREADS_WRAPPER
#include <threads.h>
#else
#include <c11/threads.h>
#endif

#include <XPLMDataAccess.h>

typedef struct _dataproxy_registry_t dataproxy_registry_t;

#include "errors.h"
#include "hashmap.h"
#include "lists.h"
#include "session.h"

/**
 * Used if the requested operation cannot be performed due to the #dataproxy_t being in an invalid state according
 * to the data proxy life-cycle.
 */
#define DATAPROXY_ERROR_INVALID_STATE       (DATAPROXY_ERROR_BASE + 0)

/**
 * Used if the requested operation failed because the session does not have permission to access the proxy or underlying
 * data (it is valid for operation callbacks to use this error when more detailed permission checks have failed).
 */
#define DATAPROXY_ERROR_PERMISSION_DENIED   (DATAPROXY_ERROR_BASE + 1)

/**
 * Used if the #dataproxy_t could not be registered to X-Plane.
 */
#define DATAPROXY_ERROR_REGISTRATION_FAILED (DATAPROXY_ERROR_BASE + 2)

/**
 * Used if an array operation tried to access elements outside the array's bounds.
 */
#define DATAPROXY_ERROR_ARRAY_OUT_OF_BOUNDS (DATAPROXY_ERROR_BASE + 3)

/**
 * Used if the requested type has not been defined to be handled by the #dataproxy_t.
 */
#define DATAPROXY_ERROR_UNSUPPORTED_TYPE    (DATAPROXY_ERROR_BASE + 4)

/**
 * Indicates that the #dataproxy_t is not reserved at the moment.
 */
#define DATAPROXY_STATE_INACTIVE 0

/**
 * Indicates that the #dataproxy_t has been reserved but is currently not registered to X-Plane.
 */
#define DATAPROXY_STATE_RESERVED 1

/**
 * Indicates that the #dataproxy_t has been registered to X-Plane (after reservation).
 */
#define DATAPROXY_STATE_REGISTERED 2

/**
 * Indicates that the #dataproxy_t is pending to be unregistered from X-Plane and the reservation is supposed to be
 * released afterwards.
 */
#define DATAPROXY_STATE_DROPPED 3

/**
 * The state of a data proxy; see DATAPROXY_STATE_ definitions.
 */
typedef uint8_t dataproxy_state_t;

/**
 * Only the session referenced during reservation is granted access, access from other sessions or X-Plane is denied.
 */
#define DATAPROXY_PERMISSION_SESSION 0

/**
 * Only XPRC is granted access (any session), access from X-Plane is denied.
 */
#define DATAPROXY_PERMISSION_XPRC 1

/**
 * Access is unrestricted.
 */
#define DATAPROXY_PERMISSION_ALL 2

/**
 * The initial permission to access a data proxy, see DATAPROXY_PERMISSION_ definitions; operation callbacks may conduct
 * more detailed checks and refuse access on further criteria.
 */
typedef uint8_t dataproxy_permission_t;

/**
 * Callback function to get a simple value (int/float/double) for a dataref.
 *
 * @param ref the operations reference as provided to #reserve_dataproxy()
 * @param type the requested data type (xplmType_Int/Float/Double)
 * @param dest destination pointer to write the result to
 * @return #ERROR_NONE if the result could be served, otherwise an error code explaining what went wrong
 */
typedef error_t (*dataproxy_simple_get_f)(void *ref, XPLMDataTypeID type, void *dest);

/**
 * Callback function to set a simple value (int/float/double) for a dataref.
 *
 * @param ref the operations reference as provided to #reserve_dataproxy()
 * @param type the provided data type (xplmType_Int/Float/Double)
 * @param value pointer to the value to be set; for sizes see: xptypes.h
 * @param source_session the session where the request originated from; NULL if the request does not originate from any
 * network session, such as a request received through the X-Plane API
 * @return #ERROR_NONE if the values were set, otherwise an error code explaining what went wrong
 */
typedef error_t (*dataproxy_simple_set_f)(void *ref, XPLMDataTypeID type, void *value, session_t *source_session);

/**
 * Callback function to read one or more elements from an array for a dataref.
 *
 * @param ref the operations reference as provided to #reserve_dataproxy()
 * @param type the requested data type (xplmType_IntArray/FloatArray/Data)
 * @param dest pointer to start copying the values to; for sizes see: xptypes.h
 * @param num_copied must be updated with the number of actually copied elements unless an error is being indicated
 * @param offset the element index to start reading/copying values from
 * @param count the maximum number of elements to read/copy; this is allowed to exceed the actual array length
 * @return #ERROR_NONE if all actually existing elements of the requested section of the array were copied, otherwise an
 * error code explaining what went wrong
 */
typedef error_t (*dataproxy_array_get_f)(void *ref, XPLMDataTypeID type, void *dest, int *num_copied, int offset, int count);

/**
 * Callback function to check the length of an array for a dataref.
 *
 * @param ref the operations reference as provided to #reserve_dataproxy()
 * @param type the requested data type (xplmType_IntArray/FloatArray/Data)
 * @param length must be updated with the array length unless an error is being indicated
 * @return #ERROR_NONE if successful, otherwise an error code explaining what went wrong
 */
typedef error_t (*dataproxy_array_length_f)(void *ref, XPLMDataTypeID type, int *length);

/**
 * Callback function to update one or more elements on an array for a dataref.
 *
 * If count exceeds the actual available backend array length, X-Plane API expects the maximum possible number of
 * elements to be copied. The backend array does not need to be expanded.
 *
 * @param ref the operations reference as provided to #reserve_dataproxy()
 * @param type the provided data type (xplmType_IntArray/FloatArray/Data)
 * @param values pointer to start reading the new values from; for sizes see: xptypes.h
 * @param offset the element index on the destination array to start storing the new values to
 * @param count the maximum number of elements to store
 * @param source_session the session where the request originated from; NULL if the request does not originate from any
 * network session, such as a request received through the X-Plane API
 * @return #ERROR_NONE if all actually available elements on the array were updated, otherwise an error code explaining what went wrong
 */
typedef error_t (*dataproxy_array_update_f)(void *ref, XPLMDataTypeID type, void *values, int offset, int count, session_t *source_session);

/// All callbacks implementing actual data access behind the data proxy.
typedef struct {
    /// called to get values of simple (non-array) datarefs
    dataproxy_simple_get_f simple_get;
    /// called to set values of simple (non-array) datarefs
    dataproxy_simple_set_f simple_set;
    /// called to get values of array datarefs
    dataproxy_array_get_f array_get;
    /// called to get the length of array datarefs
    dataproxy_array_length_f array_length;
    /// called to manipulate values of array datarefs
    dataproxy_array_update_f array_update;
} dataproxy_operations_t;

/**
 * Handles data proxy registration and thread-safety.
 */
typedef struct _dataproxy_registry_t {
    /// only cleanup is allowed if destruction is pending (true)
    bool destruction_pending;
    /// synchronizes access across the whole registry, including all proxies
    mtx_t mutex;

    /// registered dataref proxies indexed by their name
    hashmap_t *by_dataref_name;
} dataproxy_registry_t;

/**
 * A proxy handling a single dataref.
 *
 * Proxies are persistent until the registry that handles them gets destroyed. This also means that the actual owner
 * (implementing #dataproxy_operations_t and owning the #operations_ref) and the proxy definitions (such as handled
 * #types) can change except for the #dataref_name. This guaranteed stability allows references to be directly used
 * for X-Plane registration and eases handling for data access within XPRC (manipulation/lookup by other requests or
 * sessions) as well, as long as the registry gets shut down after everything that could access a proxy got severed
 * (which means X-Plane has to disable the plugin, server needs to have been closed and all sessions have to be
 * terminated). That also means the registry may even survive server soft-restarts (without disabling the plugin in
 * X-Plane).
 */
typedef struct {
    /// the registry controlling this proxy
    dataproxy_registry_t *registry;
    /// current proxy state; see DATAPROXY_STATE_*
    dataproxy_state_t state;

    /// name of the dataref as to be registered in X-Plane
    char *dataref_name;
    /// X-Plane reference as returned and used by the SDK
    XPLMDataRef xp_dataref;
    /// hosted data types as defined by the X-Plane SDK; multiple types can be combined via OR
    XPLMDataTypeID types;
    /// write access control before routing; see DATAPROXY_PERMISSION_*
    dataproxy_permission_t write_permission;

    /// the session that created and thus owns this proxy
    session_t *owner_session;
    /// callbacks handling actual data processing/storage on backend
    dataproxy_operations_t operations;
    /// passed to backend operation callbacks for context as provided during creation
    void *operations_ref;
} dataproxy_t;

/**
 * Creates a new registry.
 * @param registry will be set to created registry
 * @return error code; #ERROR_NONE if successful
 */
error_t create_dataproxy_registry(dataproxy_registry_t **registry);

/**
 * Attempts to destroy the registry; may fail.
 * @param registry the registry to be destroyed
 * @return error code; #ERROR_NONE if successful
 */
error_t destroy_dataproxy_registry(dataproxy_registry_t *registry);

/**
 * Attempts to lock the registry; proxies are locked only via the registry so holding a lock to the registry effectively
 * also locks all associated proxies.
 * @param registry the registry to be locked
 * @return error code; #ERROR_NONE if successful
 */
error_t lock_dataproxy_registry(dataproxy_registry_t *registry);

/**
 * Unlocks the registry; must only be called when a lock is actually being held.
 * @param registry the registry to be unlocked
 */
void unlock_dataproxy_registry(dataproxy_registry_t *registry);

/**
 * Attempts to lock the registry managing the given data proxy; proxies are locked only via the registry so holding a
 * lock to one data proxy/its registry effectively also locks all other associated proxies.
 * @param proxy the data proxy whose registry should be locked
 * @return error code; #ERROR_NONE if successful
 */
error_t lock_dataproxy(dataproxy_t *proxy); // TODO: not used outside dataproxy itself; privatize?

/**
 * Unlocks the registry managing the given data proxy; must only be called when a lock is actually being held.
 * @param proxy the data proxy whose registry should be unlocked
 */
void unlock_dataproxy(dataproxy_t *proxy); // TODO: not used outside dataproxy itself; privatize?

/**
 * Searches all proxies in #DATAPROXY_STATE_DROPPED state and calls #unregister_dataproxy() which also
 * releases them at the same time.
 *
 * This function must only be run within an X-Plane context and should be called regularly for maintenance.
 *
 * @param registry the registry whose dropped proxies should be unregistered
 * @return error code; #ERROR_NONE if successful
 */
error_t unregister_dropped_dataproxies(dataproxy_registry_t *registry); // run only in XP context

// used by owners
/**
 * Requests reservation of a proxy for hosting a dataref.
 *
 * Reservation may fail for several reasons, such as the dataref already having been reserved. The proxy needs to be
 * registered before it can be accessed by X-Plane or through XPRC. It must either be unregistered and released or
 * dropped when it can no longer be served.
 *
 * Data proxies can be reserved at any time, it is not necessary to be in an active X-Plane context.
 *
 * @param registry reference to the registry to link with
 * @param dataref_name name/path of the dataref as it should appear in X-Plane
 * @param types supported types; OR'ed variables from xplmType_ enum constants found in X-Plane's XPLMDataAccess.h
 * @param write_permission controls which level of write requests are routed to the operations
 * @param operations_ref pointer forwarded to all callbacks in #dataproxy_operations_t
 * @param session the #session_t which registers the data proxy
 * @param operations callbacks handling actual data access
 * @return the data proxy or NULL if reservation was not possible
 */
dataproxy_t* reserve_dataproxy(dataproxy_registry_t *registry, char *dataref_name, XPLMDataTypeID types, dataproxy_permission_t write_permission, void *operations_ref, session_t *session, dataproxy_operations_t operations);

/**
 * Releases a data proxy that is not registered to X-Plane.
 *
 * In case releasing the proxy fails, the error must be treated responsibly. As long as the proxy is not released, the
 * dataref will remain blocked within XPRC. It may be valid to try #drop_dataproxy() instead to delegate responsibility
 * for later clean up to maintenance.
 *
 * When successful, the data proxy must no longer be interacted with by the previous owner. It is recommended to
 * immediately null the reference.
 *
 * Data proxies can be released at any time, as long as they are currently not registered to X-Plane.
 * It is not necessary to be in an active X-Plane context.
 *
 * @param proxy data proxy to be released, must not be registered to X-Plane
 * @return error code; #ERROR_NONE if successful
 */
error_t release_dataproxy(dataproxy_t *proxy);

/**
 * Registers a data proxy to X-Plane which makes it generally available as both a dataref within the simulator
 * as well as open access through XPRC internally.
 *
 * Upon successful registration, backend data operations will start getting called both from X-Plane and XPRC-internal
 * contexts.
 *
 * This function must only be run within an X-Plane context.
 *
 * @param proxy data proxy to register to X-Plane and activate in XPRC
 * @return error code; #ERROR_NONE if successful
 */
error_t register_dataproxy(dataproxy_t *proxy);

/**
 * Unregisters a data proxy from X-Plane and also closes access from within XPRC.
 *
 * The data proxy will remain reserved after registration has been revoked, #release_dataproxy() must be called when a
 * full release is wanted. Special care must be taken if unregistering fails as access to the dataref will remain active
 * from X-Plane and XPRC, so backend data operations are still being called.
 *
 * If it is intended to also #release_dataproxy() but without requiring any specific timing, it may be more desirable to
 * use #drop_dataproxy() instead to immediately close access and delegate responsibility for later clean up to
 * maintenance.
 *
 * This function must only be run within an X-Plane context.
 *
 * @param proxy data proxy to unregister from X-Plane and deactivate in XPRC
 * @return error code; #ERROR_NONE if successful
 */
error_t unregister_dataproxy(dataproxy_t *proxy);

/**
 * Immediately closes access to backend operations and delegates responsibility for the data proxy to maintenance for
 * deferred clean-up in case no control over the dataref is required.
 *
 * Dropping a proxy is an easy way for previous owners to get rid of any obligations for maintaining backend operations
 * and calling #unregister_dataproxy() and #release_dataproxy(). When successful, regular maintenance calls to
 * #unregister_dropped_dataproxies() will take care of deactivating registration and releasing the proxy when possible.
 * Operations are immediately rerouted to an XPRC-internal stub which will result in invalid data getting returned by
 * dataref queries until the dataref could be unregistered. The data proxy must no longer be interacted with by the
 * previous owner. It is recommended to immediately null the reference.
 *
 * Special care must be taken if dropping fails as access to the dataref will remain active from X-Plane and XPRC, so
 * backend data operations are still being called.
 *
 * Data proxies can be dropped at any time, it is not necessary to be in an active X-Plane context.
 *
 * @param proxy data proxy to drop
 * @return error code; #ERROR_NONE if successful
 */
error_t drop_dataproxy(dataproxy_t *proxy);

// used for access throughout XPRC (not limited to owners)
// operations and internals should never be interacted with directly, use these functions instead

/**
 * Searches for the specified dataref as a registered (active) proxy hosted by XPRC.
 *
 * If the dataref is not hosted through a data proxy within XPRC or the proxy is currently not registered (active)
 * or some error occurred during lookup, NULL will be returned instead of a data proxy reference.
 *
 * @param registry the registry to search in
 * @param dataref_name name/path of the dataref to search for
 * @return the data proxy if found; NULL otherwise
 */
dataproxy_t* find_registered_dataproxy(dataproxy_registry_t *registry, char *dataref_name);

/**
 * Lists all data proxies currently having the wanted #dataproxy_state_t.
 *
 * The list being returned is a structurally static copy of pointers to the original #dataproxy_t instances.
 * While the list is owned by the caller and must be freed properly when discarded, the data proxies (list values)
 * must not be managed/freed as they are shared pointers managed by their respective owners.
 *
 * It may be desirable to lock the registry before query and keep it locked while processing the result, otherwise
 * data proxies may change states concurrently.
 *
 * @param registry the registry to search in
 * @param wanted_state data proxy state to search for
 * @return a list of all matching proxies, may be empty; NULL on error
 */
prealloc_list_t* list_dataproxies_with_state(dataproxy_registry_t *registry, dataproxy_state_t wanted_state);

/**
 * @deprecated API under review, do not use
 */
error_t dataproxy_get_name(dataproxy_t *proxy, char **dest); // FIXME: copy may be better; currently unused - see implementation

/**
 * Copies the data proxy's currently registered X-Plane data types onto the destination pointer.
 *
 * The data proxy may host multiple types at once. In the same way as on X-Plane API the types will be OR'ed bitwise in
 * that case, to combine into a single XPLMDataTypeID.
 *
 * Data proxies which are not registered at time of call cannot be queried for their type as it is not guaranteed that
 * the information would be accurate.
 *
 * In case of errors, including queries to unregistered data proxies, the destination value will be reset to
 * xplmType_Unknown.
 *
 * It may be desirable to lock the registry before query and keep it locked while processing the result, otherwise
 * data proxies may change types or availability concurrently.
 *
 * @param proxy data proxy to be queried
 * @param dest pointer to write result value to; set to xplmType_Unknown on error
 * @return error code; #ERROR_NONE if successful
 */
error_t dataproxy_get_types(dataproxy_t *proxy, XPLMDataTypeID *dest);

/**
 * Checks if the data proxy can currently be written to by the given #session_t.
 *
 * Only routing level access can be checked by this function (as defined during #reserve_dataproxy()), actual data
 * access may still be prevented by backend operation logic.
 *
 * It may be desirable to lock the registry before query and keep it locked while processing the result, otherwise
 * data proxies may change permissions or availability concurrently.
 *
 * @param proxy data proxy to be checked
 * @param session session to check access for; set to NULL if no session is applicable, such as general access through X-Plane
 * @return true if write-access is permitted on routing level, false otherwise
 */
bool dataproxy_can_write(dataproxy_t *proxy, session_t *session);

/**
 * Queries the given data proxy for the specified simple data type (xplmType_Int/Float/Double).
 *
 * Values can only be retrieved from registered (active) data proxies hosting the wanted type. The result will be
 * written into the specified destination.
 *
 * In case of errors (return value is not #ERROR_NONE), destination value may or may not get overwritten. As error cases
 * result in unspecified behaviour; the value will probably not make any sense and should be ignored.
 *
 * @param proxy data proxy to be queried
 * @param type data type to retrieve the value of; must be exactly one type (no combination)
 * @param dest pointer to store result value in; must have correct size/type; manipulation unspecified on error
 * @return error code; #ERROR_NONE if successful
 */
error_t dataproxy_simple_get(dataproxy_t *proxy, XPLMDataTypeID type, void *dest);

/**
 * Attempts to set the specified simple value (xplmType_Int/Float/Double) on the given data proxy.
 *
 * Only registered (active) data proxies hosting the correct type can be written to. Write access may get denied by
 * backend operations, even if #dataproxy_can_write() indicates true for request routing.
 *
 * @param proxy data proxy to manipulate
 * @param type type of value; must be exactly one type (no combination)
 * @param value pointer to the value to be set on the proxy; must have correct size/type
 * @param source_session the session requesting manipulation, used to check write permission on routing level; set to NULL if no session is applicable, such as general access through X-Plane
 * @return error code; #ERROR_NONE if successful
 */
error_t dataproxy_simple_set(dataproxy_t *proxy, XPLMDataTypeID type, void *value, session_t *source_session);

/**
 * Queries the given data proxy for the specified array data type (xplmType_IntArray/FloatArray/Data).
 *
 * Values can only be retrieved from registered (active) data proxies hosting the wanted type. The result will be
 * written into the specified destination.
 *
 * The given offset (zero-based) determines the first item of the data proxy's array to be read from. While the offset
 * has to exist on the array, the count can exceed the actual array length of the data proxy. Copying will stop
 * successfully on the last array element. The actual number of copied elements will be indicated through num_copied
 * afterwards. However, the destination array must be large enough to fit the maximum requested count.
 *
 * In case of errors (return value is not #ERROR_NONE), destination and num_copied values may or may not get
 * overwritten. As error cases result in unspecified behaviour; the values will probably not make any sense and should
 * be ignored.
 *
 * @param proxy data proxy to be queried
 * @param type data type to retrieve the value of; must be exactly one type (no combination)
 * @param dest pointer to store result value in; must have correct size/type; manipulation unspecified on error
 * @param num_copied pointer to store the number of actually copied elements in
 * @param offset index of data proxy's first item to start copying from (zero-based)
 * @param count maximum number of items to copy, starting from the specified offset
 * @return error code; #ERROR_NONE if successful
 */
error_t dataproxy_array_get(dataproxy_t *proxy, XPLMDataTypeID type, void *dest, int *num_copied, int offset, int count);

/**
 * Queries the given data proxy for the array length of the specified data type (xplmType_IntArray/FloatArray/Data).
 *
 * Lengths can only be retrieved from registered (active) data proxies hosting the wanted type. The result will be
 * written into the specified reference.
 *
 * In case of errors, including queries to unregistered data proxies, the length will be indicated as 0.
 *
 * It may be desirable to lock the registry before query and keep it locked while processing the result, otherwise
 * data proxies may change array length, types or availability concurrently.
 *
 * Array lengths are specific to each of the proxy's data types as per X-Plane API.
 *
 * @param proxy data proxy to be queried
 * @param type data type to retrieve the value of; must be exactly one type (no combination)
 * @param length pointer where to store the returned length in; 0 on error
 * @return error code; #ERROR_NONE if successful
 */
error_t dataproxy_array_length(dataproxy_t *proxy, XPLMDataTypeID type, int *length);

/**
 * Attempts to set the specified array values (xplmType_IntArray/FloatArray/Data) on the given data proxy.
 *
 * Only registered (active) data proxies hosting the correct type can be written to. Write access may get denied by
 * backend operations, even if #dataproxy_can_write() indicates true for request routing.
 *
 * The given offset (zero-based) determines the first item of the data proxy's array to be manipulated. While the offset
 * has to exist on the array, the count can exceed the actual array length of the data proxy. Copying will stop
 * successfully on the last array element. The provided values array must hold at least as many elements as the maximum
 * requested count.
 *
 * @param proxy data proxy to manipulate
 * @param type type of values; must be exactly one type (no combination)
 * @param values pointer to the values to be set on the proxy; must have correct size/type
 * @param offset index of data proxy's first item to be copied to (zero-based)
 * @param count maximum number of items to copy, starting from the specified offset
 * @param source_session the session requesting manipulation, used to check write permission on routing level; set to NULL if no session is applicable, such as general access through X-Plane
 * @return error code; #ERROR_NONE if successful
 */
error_t dataproxy_array_update(dataproxy_t *proxy, XPLMDataTypeID type, void *values, int offset, int count, session_t *source_session);

#endif
