#ifndef DATAPROXY_H
#define DATAPROXY_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <threads.h>

#include <XPLMDataAccess.h>

typedef struct _dataproxy_registry_t dataproxy_registry_t;

#include "errors.h"
#include "hashmap.h"
#include "lists.h"
#include "session.h"

#define DATAPROXY_ERROR_INVALID_STATE       (DATAPROXY_ERROR_BASE + 0)
#define DATAPROXY_ERROR_PERMISSION_DENIED   (DATAPROXY_ERROR_BASE + 1)
#define DATAPROXY_ERROR_REGISTRATION_FAILED (DATAPROXY_ERROR_BASE + 2)
#define DATAPROXY_ERROR_ARRAY_OUT_OF_BOUNDS (DATAPROXY_ERROR_BASE + 3)
#define DATAPROXY_ERROR_UNSUPPORTED_TYPE    (DATAPROXY_ERROR_BASE + 4)

#define DATAPROXY_STATE_INACTIVE 0
#define DATAPROXY_STATE_RESERVED 1
#define DATAPROXY_STATE_REGISTERED 2

typedef uint8_t dataproxy_state_t;

#define DATAPROXY_PERMISSION_SESSION 0
#define DATAPROXY_PERMISSION_XPRC 1
#define DATAPROXY_PERMISSION_ALL 2

typedef uint8_t dataproxy_permission_t;

typedef error_t (*dataproxy_simple_get_f)(void *ref, XPLMDataTypeID type, void *dest);
typedef error_t (*dataproxy_simple_set_f)(void *ref, XPLMDataTypeID type, void *value, session_t *source_session);
typedef error_t (*dataproxy_array_get_f)(void *ref, XPLMDataTypeID type, void *dest, int *num_copied, int offset, int count);
typedef error_t (*dataproxy_array_length_f)(void *ref, XPLMDataTypeID type, int *length);
typedef error_t (*dataproxy_array_update_f)(void *ref, XPLMDataTypeID type, void *values, int offset, int count, session_t *source_session);

typedef struct {
    dataproxy_simple_get_f simple_get;
    dataproxy_simple_set_f simple_set;
    dataproxy_array_get_f array_get;
    dataproxy_array_length_f array_length;
    dataproxy_array_update_f array_update;
} dataproxy_operations_t;

/* Proxies are persistent until registry gets destroyed, even if owner and definitions change in between.
 * This allows block references to be directly used for X-Plane registration and eases handling for data access from other
 * XPRC sessions as well.
 */
typedef struct _dataproxy_registry_t {
    bool destruction_pending;
    mtx_t mutex;

    hashmap_t *by_dataref_name;
} dataproxy_registry_t;

typedef struct {
    dataproxy_registry_t *registry;
    dataproxy_state_t state;
    
    char *dataref_name;
    XPLMDataRef xp_dataref;
    XPLMDataTypeID types;
    dataproxy_permission_t write_permission;
    
    session_t *owner_session;
    dataproxy_operations_t operations;
    void *operations_ref;
} dataproxy_t;

error_t create_dataproxy_registry(dataproxy_registry_t **registry);
error_t destroy_dataproxy_registry(dataproxy_registry_t *registry);

error_t lock_dataproxy_registry(dataproxy_registry_t *registry);
void unlock_dataproxy_registry(dataproxy_registry_t *registry);
error_t lock_dataproxy(dataproxy_t *proxy);
void unlock_dataproxy(dataproxy_t *proxy);

// used by owners
dataproxy_t* reserve_dataproxy(dataproxy_registry_t *registry, char *dataref_name, XPLMDataTypeID types, dataproxy_permission_t write_permission, void *operations_ref, session_t *session, dataproxy_operations_t operations);
error_t release_dataproxy(dataproxy_t *proxy); // must be unregistered before release
error_t register_dataproxy(dataproxy_t *proxy); // run only in XP context; needed for internal activation as well
error_t unregister_dataproxy(dataproxy_t *proxy); // run only in XP context; needed for internal activation as well

// used for access within XPRC
// operations and internals should never be interacted with directly, use these functions instead
dataproxy_t* find_registered_dataproxy(dataproxy_registry_t *registry, char *dataref_name);
prealloc_list_t* list_registered_dataproxies(dataproxy_registry_t *registry); // returns a static copy of pointers to all registered proxies at time of call; array is to be managed/destroyed by caller but values must not be freed!
error_t dataproxy_get_name(dataproxy_t *proxy, char **dest); // do not modify dest pointer content, it's not a copy
error_t dataproxy_get_types(dataproxy_t *proxy, XPLMDataTypeID *dest);
bool dataproxy_can_write(dataproxy_t *proxy, session_t *session);
error_t dataproxy_simple_get(dataproxy_t *proxy, XPLMDataTypeID type, void *dest);
error_t dataproxy_simple_set(dataproxy_t *proxy, XPLMDataTypeID type, void *value, session_t *source_session);
error_t dataproxy_array_get(dataproxy_t *proxy, XPLMDataTypeID type, void *dest, int *num_copied, int offset, int count);
error_t dataproxy_array_length(dataproxy_t *proxy, XPLMDataTypeID type, int *length);
error_t dataproxy_array_update(dataproxy_t *proxy, XPLMDataTypeID type, void *values, int offset, int count, session_t *source_session);

#endif
