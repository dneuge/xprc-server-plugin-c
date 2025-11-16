#include <string.h>

#include "utils.h"
#include "xptypes.h"

#include "dataproxy.h"

#include "logger.h"

error_t create_dataproxy_registry(dataproxy_registry_t **registry) {
    *registry = zalloc(sizeof(dataproxy_registry_t));
    if (!(*registry)) {
        return ERROR_MEMORY_ALLOCATION;
    }

    if (mtx_init(&(*registry)->mutex, mtx_plain | mtx_recursive) != thrd_success) {
        free(*registry);
        *registry = NULL;
        return ERROR_UNSPECIFIC;
    }

    (*registry)->by_dataref_name = create_hashmap();
    if (!(*registry)->by_dataref_name) {
        mtx_destroy(&(*registry)->mutex);
        free(*registry);
        *registry = NULL;
        return ERROR_MEMORY_ALLOCATION;
    }

    return ERROR_NONE;
}


static void destroy_dataproxy(dataproxy_t *proxy) {
    if (proxy->state != DATAPROXY_STATE_INACTIVE) {
        RCLOG_ERROR("dataproxy is active and must not be destroyed; this is a memleak and may be followed by a crash: %s", proxy->dataref_name);
        return;
    }

    if (proxy->dataref_name) {
        free(proxy->dataref_name);
        proxy->dataref_name = NULL;
    }
    
    free(proxy);
}

static void destroy_dataproxy_hashmap_value(char *_key, void *proxy) {
    destroy_dataproxy((dataproxy_t*) proxy);
}

error_t destroy_dataproxy_registry(dataproxy_registry_t *registry) {
    if (registry->destruction_pending) {
        // called twice?!
        return ERROR_DESTRUCTION_PENDING;
    }

    if (mtx_lock(&registry->mutex) != thrd_success) {
        return ERROR_MUTEX_FAILED;
    }

    if (registry->destruction_pending) {
        // called twice?!
        mtx_unlock(&registry->mutex);
        return ERROR_DESTRUCTION_PENDING;
    }

    registry->destruction_pending = true;

    // unlock, relock, unlock to give every thread a chance to notice that destruction is pending now
    mtx_unlock(&registry->mutex);
    if (mtx_lock(&registry->mutex) != thrd_success) {
        return ERROR_MUTEX_FAILED;
    }
    mtx_unlock(&registry->mutex);

    destroy_hashmap(registry->by_dataref_name, destroy_dataproxy_hashmap_value);
    registry->by_dataref_name = NULL;
    
    mtx_destroy(&registry->mutex);

    free(registry);

    return ERROR_NONE;
}

error_t lock_dataproxy_registry(dataproxy_registry_t *registry) {
    if (!registry) {
        RCLOG_ERROR("[dataproxy] lock_dataproxy_registry called with NULL");
        return ERROR_UNSPECIFIC;
    }
    
    if (registry->destruction_pending) {
        return ERROR_DESTRUCTION_PENDING;
    }
    
    if (mtx_lock(&registry->mutex) != thrd_success) {
        return ERROR_MUTEX_FAILED;
    }

    if (registry->destruction_pending) {
        mtx_unlock(&registry->mutex);
        return ERROR_DESTRUCTION_PENDING;
    }
    
    return ERROR_NONE;
}

void unlock_dataproxy_registry(dataproxy_registry_t *registry) {
    mtx_unlock(&registry->mutex);
}

error_t lock_dataproxy(dataproxy_t *proxy) {
    if (!proxy) {
        RCLOG_ERROR("[dataproxy] lock_dataproxy called with NULL");
        return ERROR_UNSPECIFIC;
    }
    
    return lock_dataproxy_registry(proxy->registry);
}

void unlock_dataproxy(dataproxy_t *proxy) {
    unlock_dataproxy_registry(proxy->registry);
}

static void clear_dataproxy(dataproxy_t *proxy) {
    // state, registry and dataref_name are not cleared!
    proxy->xp_dataref = NO_XP_DATAREF;
    proxy->types = xplmType_Unknown;
    proxy->write_permission = DATAPROXY_PERMISSION_SESSION;
    proxy->owner_session = NULL;
    proxy->operations_ref = NULL;
    
    memset(&proxy->operations, 0, sizeof(dataproxy_operations_t));
}

static dataproxy_t* create_dataproxy(dataproxy_registry_t *registry, char *dataref_name) {
    dataproxy_t *proxy = zalloc(sizeof(dataproxy_t));
    if (!proxy) {
        return NULL;
    }

    clear_dataproxy(proxy);
    
    proxy->dataref_name = copy_string(dataref_name);
    if (!proxy->dataref_name) {
        free(proxy);
        return NULL;
    }

    proxy->state = DATAPROXY_STATE_INACTIVE;
    proxy->registry = registry;

    return proxy;
}

static const XPLMDataTypeID mask_all_types = xplmType_Int | xplmType_Float | xplmType_Double | xplmType_FloatArray | xplmType_IntArray | xplmType_Data;

static bool is_valid_combined_type(XPLMDataTypeID types) {
    return (types & ~mask_all_types) == 0;
}

static bool is_valid_write_permission(dataproxy_permission_t write_permission) {
    return (write_permission == DATAPROXY_PERMISSION_SESSION)
        || (write_permission == DATAPROXY_PERMISSION_XPRC)
        || (write_permission == DATAPROXY_PERMISSION_ALL);
}

static bool has_all_operations(dataproxy_operations_t *operations) {
    return operations->simple_get
        && operations->simple_set
        && operations->array_get
        && operations->array_length
        && operations->array_update;
}

dataproxy_t* reserve_dataproxy(dataproxy_registry_t *registry, char *dataref_name, XPLMDataTypeID types, dataproxy_permission_t write_permission, void *operations_ref, session_t *session, dataproxy_operations_t operations) {
    RCLOG_TRACE("[dataproxy] reserve");
    
    bool is_valid = is_valid_combined_type(types) && is_valid_write_permission(write_permission) && has_all_operations(&operations);
    if (!is_valid) {
        RCLOG_DEBUG("[dataproxy] reserve: invalid");
        return NULL;
    }

    RCLOG_TRACE("[dataproxy] reserve: locking");
    if (lock_dataproxy_registry(registry) != ERROR_NONE) {
        RCLOG_WARN("[dataproxy] reserve: lock failed");
        return NULL;
    }

    RCLOG_TRACE("[dataproxy] reserve: get existing proxy");
    
    dataproxy_t *proxy = hashmap_get(registry->by_dataref_name, dataref_name);
    if (!proxy) {
        RCLOG_TRACE("[dataproxy] reserve: no proxy found");
        proxy = create_dataproxy(registry, dataref_name);
        if (!proxy) {
            unlock_dataproxy_registry(registry);
            return NULL;
        }

        RCLOG_TRACE("[dataproxy] reserve: putting proxy to map");
        dataproxy_t *old_proxy = NULL;
        if (!hashmap_put(registry->by_dataref_name, dataref_name, proxy, (void**) &old_proxy)) {
            unlock_dataproxy_registry(registry);
            destroy_dataproxy(proxy);
            return NULL;
        }

        if (old_proxy) {
            // there's no reasonable way to rollback and abort, just log
            RCLOG_ERROR("dataproxy registry detected concurrent modification during reservation; expect memleak and crash: %s", dataref_name);
        }
    }

    if (proxy->state == DATAPROXY_STATE_DROPPED) {
        // previous dataref is still registered after owner dropped it,
        // we need to unregister the dataref before we can redefine it
        RCLOG_DEBUG("[dataproxy] reserve: proxy was dropped, unregistering");
        error_t err = unregister_dataproxy(proxy);
        if (err != ERROR_NONE) {
            RCLOG_WARN("[dataproxy] reserve: unregistering dropped proxy failed: %d", err);
            unlock_dataproxy_registry(registry);
            return NULL;
        }
    }

    if (proxy->state != DATAPROXY_STATE_INACTIVE) {
        RCLOG_WARN("[dataproxy] reserve: bad state: %d", proxy->state);
        unlock_dataproxy_registry(registry);
        return NULL;
    }

    proxy->state = DATAPROXY_STATE_RESERVED;
    proxy->types = types;
    proxy->write_permission = write_permission;
    proxy->operations_ref = operations_ref;
    proxy->owner_session = session;
    proxy->operations = operations;
    
    unlock_dataproxy_registry(registry);

    RCLOG_TRACE("[dataproxy] reserve: done");
    
    return proxy;
}

error_t release_dataproxy(dataproxy_t *proxy) {
    error_t err = ERROR_NONE;
    error_t out_err = ERROR_NONE;
    
    err = lock_dataproxy(proxy);
    if (err != ERROR_NONE) {
        return err;
    }

    if (proxy->state != DATAPROXY_STATE_RESERVED) {
        out_err = DATAPROXY_ERROR_INVALID_STATE;
    } else {
        proxy->state = DATAPROXY_STATE_INACTIVE;
        clear_dataproxy(proxy);
    }

    unlock_dataproxy(proxy);

    return out_err;
}

static int dataproxy_xp_get_data_integer(void *inRefcon) {
    dataproxy_t *proxy = inRefcon;
    xpint_t value = 0;

    error_t err = dataproxy_simple_get(proxy, xplmType_Int, &value);
    if (err != ERROR_NONE) {
        return 0;
    }
    
    return value;
}

static void dataproxy_xp_set_data_integer(void *inRefcon, int inValue) {
    dataproxy_simple_set(inRefcon, xplmType_Int, &inValue, NULL);
}

static float dataproxy_xp_get_data_float(void *inRefcon) {
    dataproxy_t *proxy = inRefcon;
    xpfloat_t value = 0.0f;

    error_t err = dataproxy_simple_get(proxy, xplmType_Float, &value);
    if (err != ERROR_NONE) {
        return 0.0f;
    }
    
    return value;
}

static void dataproxy_xp_set_data_float(void *inRefcon, float inValue) {
    dataproxy_simple_set(inRefcon, xplmType_Float, &inValue, NULL);
}

static double dataproxy_xp_get_data_double(void *inRefcon) {
    dataproxy_t *proxy = inRefcon;
    xpdouble_t value = 0.0;

    error_t err = dataproxy_simple_get(proxy, xplmType_Double, &value);
    if (err != ERROR_NONE) {
        return 0.0;
    }
    
    return value;
}

static void dataproxy_xp_set_data_double(void *inRefcon, double inValue) {
    dataproxy_simple_set(inRefcon, xplmType_Double, &inValue, NULL);
}

static int dataproxy_xp_get_data_array(void *inRefcon, XPLMDataTypeID type, void *outValues, int inOffset, int inMax) {
    dataproxy_t *proxy = inRefcon;
    error_t err = ERROR_NONE;

    int out = 0;

    RCLOG_TRACE("[dataproxy] dataproxy_xp_get_data_array type=%d, outValues=%p, inOffset=%d, inMax=%d", type, outValues, inOffset, inMax);
    
    if (outValues) {
        RCLOG_TRACE("[dataproxy] dataproxy_xp_get_data_array => dataproxy_array_get");
        err = dataproxy_array_get(proxy, type, outValues, &out, inOffset, inMax);
    } else {
        RCLOG_TRACE("[dataproxy] dataproxy_xp_get_data_array => dataproxy_array_length");
        err = dataproxy_array_length(proxy, type, &out);
    }
    
    RCLOG_TRACE("[dataproxy] dataproxy_xp_get_data_array: out=%d, err=%d", out, err);
    
    if (err != ERROR_NONE) {
        return 0;
    }

    return out;
}

static int dataproxy_xp_get_data_integer_array(void *inRefcon, int *outValues, int inOffset, int inMax) {
    return dataproxy_xp_get_data_array(inRefcon, xplmType_IntArray, outValues, inOffset, inMax);
}

static void dataproxy_xp_set_data_integer_array(void *inRefcon, int *inValues, int inOffset, int inCount) {
    dataproxy_array_update(inRefcon, xplmType_IntArray, inValues, inOffset, inCount, NULL);
}

static int dataproxy_xp_get_data_float_array(void *inRefcon, float *outValues, int inOffset, int inMax) {
    return dataproxy_xp_get_data_array(inRefcon, xplmType_FloatArray, outValues, inOffset, inMax);
}

static void dataproxy_xp_set_data_float_array(void *inRefcon, float *inValues, int inOffset, int inCount) {
    dataproxy_array_update(inRefcon, xplmType_FloatArray, inValues, inOffset, inCount, NULL);
}

static int dataproxy_xp_get_data_byte_array(void *inRefcon, void *outValue, int inOffset, int inMaxLength) {
    return dataproxy_xp_get_data_array(inRefcon, xplmType_Data, outValue, inOffset, inMaxLength);
}

static void dataproxy_xp_set_data_byte_array(void *inRefcon, void *inValue, int inOffset, int inLength) {
    dataproxy_array_update(inRefcon, xplmType_Data, inValue, inOffset, inLength, NULL);
}

error_t register_dataproxy(dataproxy_t *proxy) {
    error_t err = ERROR_NONE;
    error_t out_err = ERROR_NONE;
    
    err = lock_dataproxy(proxy);
    if (err != ERROR_NONE) {
        return err;
    }

    if (proxy->state != DATAPROXY_STATE_RESERVED) {
        out_err = DATAPROXY_ERROR_INVALID_STATE;
    } else {
        proxy->xp_dataref = XPLMRegisterDataAccessor(proxy->dataref_name,
                                                     proxy->types,
                                                     (proxy->write_permission == DATAPROXY_PERMISSION_ALL) ? 1 : 0,
                                                     dataproxy_xp_get_data_integer,
                                                     dataproxy_xp_set_data_integer,
                                                     dataproxy_xp_get_data_float,
                                                     dataproxy_xp_set_data_float,
                                                     dataproxy_xp_get_data_double,
                                                     dataproxy_xp_set_data_double,
                                                     dataproxy_xp_get_data_integer_array,
                                                     dataproxy_xp_set_data_integer_array,
                                                     dataproxy_xp_get_data_float_array,
                                                     dataproxy_xp_set_data_float_array,
                                                     dataproxy_xp_get_data_byte_array,
                                                     dataproxy_xp_set_data_byte_array,
                                                     proxy,
                                                     proxy);

        if (proxy->xp_dataref == NO_XP_DATAREF) {
            out_err = DATAPROXY_ERROR_REGISTRATION_FAILED;
        } else {
            proxy->state = DATAPROXY_STATE_REGISTERED;
        }
    }

    unlock_dataproxy(proxy);

    return out_err;
}

error_t unregister_dataproxy(dataproxy_t *proxy) {
    error_t err = ERROR_NONE;
    error_t out_err = ERROR_NONE;
    
    err = lock_dataproxy(proxy);
    if (err != ERROR_NONE) {
        return err;
    }

    if (proxy->state == DATAPROXY_STATE_REGISTERED) {
        XPLMUnregisterDataAccessor(proxy->xp_dataref);
        
        proxy->state = DATAPROXY_STATE_RESERVED;
        proxy->xp_dataref = NO_XP_DATAREF;
    } else if (proxy->state == DATAPROXY_STATE_DROPPED) {
        XPLMUnregisterDataAccessor(proxy->xp_dataref);

        proxy->state = DATAPROXY_STATE_INACTIVE;
        clear_dataproxy(proxy);
    } else {
        out_err = DATAPROXY_ERROR_INVALID_STATE;
    }

    unlock_dataproxy(proxy);

    return out_err;
}

error_t drop_dataproxy(dataproxy_t *proxy) {
    error_t err = ERROR_NONE;
    error_t out_err = ERROR_NONE;
    
    err = lock_dataproxy(proxy);
    if (err != ERROR_NONE) {
        return err;
    }

    if (proxy->state != DATAPROXY_STATE_REGISTERED) {
        out_err = DATAPROXY_ERROR_INVALID_STATE;
    } else {
        proxy->state = DATAPROXY_STATE_DROPPED;
        proxy->owner_session = NULL;
        proxy->operations_ref = NULL;
        memset(&proxy->operations, 0, sizeof(dataproxy_operations_t));
    }

    unlock_dataproxy(proxy);

    return out_err;
}

dataproxy_t* find_registered_dataproxy(dataproxy_registry_t *registry, char *dataref_name) {
    error_t err = ERROR_NONE;

    if (!dataref_name) {
        return NULL;
    }

    err = lock_dataproxy_registry(registry);
    if (err != ERROR_NONE) {
        return NULL;
    }

    dataproxy_t *proxy = hashmap_get(registry->by_dataref_name, dataref_name);
    if (proxy && (proxy->state != DATAPROXY_STATE_REGISTERED)) {
        proxy = NULL;
    }

    unlock_dataproxy_registry(registry);

    return proxy;
}

prealloc_list_t* list_dataproxies_with_state(dataproxy_registry_t *registry, dataproxy_state_t wanted_state) {
    error_t err = ERROR_NONE;

    prealloc_list_t *out = create_preallocated_list();
    if (!out) {
        return NULL;
    }
    
    err = lock_dataproxy_registry(registry);
    if (err != ERROR_NONE) {
        return NULL;
    }

    hashmap_item_t **hashmap_root_items = registry->by_dataref_name->items;
    hashmap_item_t *item = NULL;
    for (int i=0; i<HASH_COMBINATIONS; i++) {
        item = hashmap_root_items[i];
        while (item) {
            dataproxy_t *proxy = item->value;
            if (proxy && proxy->state == wanted_state) {
                if (!prealloc_list_append(out, proxy)) {
                    // memory allocation failed; abort
                    destroy_preallocated_list(out, NULL, PREALLOC_LIST_OVERRIDE_DEFERRED_DESTRUCTORS);
                    unlock_dataproxy_registry(registry);
                    return NULL;
                }
            }
            
            item = item->next;
        }
    }

    unlock_dataproxy_registry(registry);

    return out;
}

error_t dataproxy_get_name(dataproxy_t *proxy, char **dest) {
    // QUESTION: would it be better to lock and copy the char name despite being immutable? (unless asked for the constant?)
    
    if (!proxy || !proxy->registry || proxy->registry->destruction_pending) {
        return ERROR_DESTRUCTION_PENDING;
    }
    
    *dest = proxy->dataref_name;
    
    return ERROR_NONE;
}

error_t dataproxy_get_types(dataproxy_t *proxy, XPLMDataTypeID *dest) {
    error_t err = ERROR_NONE;
    error_t out_err = ERROR_NONE;
    
    err = lock_dataproxy(proxy);
    if (err != ERROR_NONE) {
        *dest = xplmType_Unknown;
        return err;
    }

    if (proxy->state != DATAPROXY_STATE_REGISTERED) {
        out_err = DATAPROXY_ERROR_INVALID_STATE;
        *dest = xplmType_Unknown;
    } else {
        *dest = proxy->types;
    }
    
    unlock_dataproxy(proxy);

    return out_err;
}

bool dataproxy_can_write(dataproxy_t *proxy, session_t *session) {
    error_t err = ERROR_NONE;
    bool out = false;
    
    err = lock_dataproxy(proxy);
    if (err != ERROR_NONE) {
        return false;
    }

    if (proxy->state == DATAPROXY_STATE_REGISTERED) {
        out = (proxy->write_permission == DATAPROXY_PERMISSION_ALL)
            || (session && ((proxy->write_permission == DATAPROXY_PERMISSION_XPRC) || (session == proxy->owner_session)));
    }
    
    unlock_dataproxy(proxy);

    return out;
}

static inline bool is_simple_type(XPLMDataTypeID type) {
    return (type == xplmType_Int) || (type == xplmType_Float) || (type == xplmType_Double);
}

error_t dataproxy_simple_get(dataproxy_t *proxy, XPLMDataTypeID type, void *dest) {
    error_t err = ERROR_NONE;
    
    if (!dest) {
        return ERROR_UNSPECIFIC;
    }
    
    if (!is_simple_type(type)) {
        return DATAPROXY_ERROR_UNSUPPORTED_TYPE;
    }

    err = lock_dataproxy(proxy);
    if (err != ERROR_NONE) {
        return err;
    }
    
    if (proxy->state != DATAPROXY_STATE_REGISTERED) {
        err = DATAPROXY_ERROR_INVALID_STATE;
    } else if (!proxy->operations.simple_get || (proxy->types & type) == 0) {
        err = DATAPROXY_ERROR_UNSUPPORTED_TYPE;
    } else {
        err = proxy->operations.simple_get(proxy->operations_ref, type, dest);
    }
    
    unlock_dataproxy(proxy);

    return err;
}

error_t dataproxy_simple_set(dataproxy_t *proxy, XPLMDataTypeID type, void *value, session_t *source_session) {
    error_t err = ERROR_NONE;
    
    if (!value) {
        return ERROR_UNSPECIFIC;
    }
    
    if (!is_simple_type(type)) {
        return DATAPROXY_ERROR_UNSUPPORTED_TYPE;
    }

    err = lock_dataproxy(proxy);
    if (err != ERROR_NONE) {
        return err;
    }
    
    dataproxy_simple_set_f setter = NULL;
    void *operations_ref = NULL;

    if (proxy->state != DATAPROXY_STATE_REGISTERED) {
        err = DATAPROXY_ERROR_INVALID_STATE;
    } else if (!dataproxy_can_write(proxy, source_session)) {
        err = DATAPROXY_ERROR_PERMISSION_DENIED;
    } else if (!proxy->operations.simple_set || (proxy->types & type) == 0) {
        err = DATAPROXY_ERROR_UNSUPPORTED_TYPE;
    } else {
        setter = proxy->operations.simple_set;
        operations_ref = proxy->operations_ref;
    }

    // dataproxy has to be unlocked before calling setter as it will otherwise deadlock
    // FIXME: check if this is sufficiently secured against e.g. concurrent proxy deregistration
    unlock_dataproxy(proxy);

    if (err == ERROR_NONE && setter) {
        err = setter(operations_ref, type, value, source_session);
    }

    return err;
}

static inline bool is_array_type(XPLMDataTypeID type) {
    return (type == xplmType_IntArray) || (type == xplmType_FloatArray) || (type == xplmType_Data);
}

error_t dataproxy_array_get(dataproxy_t *proxy, XPLMDataTypeID type, void *dest, int *num_copied, int offset, int count) {
    error_t err = ERROR_NONE;
    
    if (!dest || !num_copied || offset < 0 || count < 0) {
        return ERROR_UNSPECIFIC;
    }
    
    if (!is_array_type(type)) {
        return DATAPROXY_ERROR_UNSUPPORTED_TYPE;
    }

    if (count == 0) {
        return ERROR_NONE;
    }
    
    err = lock_dataproxy(proxy);
    if (err != ERROR_NONE) {
        return err;
    }

    if (proxy->state != DATAPROXY_STATE_REGISTERED) {
        err = DATAPROXY_ERROR_INVALID_STATE;
    } else if (!proxy->operations.array_get || (proxy->types & type) == 0) {
        err = DATAPROXY_ERROR_UNSUPPORTED_TYPE;
    } else {
        err = proxy->operations.array_get(proxy->operations_ref, type, dest, num_copied, offset, count);
        if (*num_copied < 0) {
            *num_copied = 0;
            if (err != ERROR_NONE) {
                err = ERROR_UNSPECIFIC;
            }
        }
    }
    
    unlock_dataproxy(proxy);

    return err;
}

error_t dataproxy_array_length(dataproxy_t *proxy, XPLMDataTypeID type, int *length) {
    error_t err = ERROR_NONE;
    error_t out_err = ERROR_NONE;

    err = lock_dataproxy(proxy);
    if (err != ERROR_NONE) {
        *length = 0;
        return err;
    }

    if (proxy->state != DATAPROXY_STATE_REGISTERED) {
        out_err = DATAPROXY_ERROR_INVALID_STATE;
        *length = 0;
    } else if (!proxy->operations.array_length || (proxy->types & type) == 0) {
        out_err = DATAPROXY_ERROR_UNSUPPORTED_TYPE;
        *length = 0;
    } else {
        int out = 0;
        err = proxy->operations.array_length(proxy->operations_ref, type, &out);
        if (out < 0) {
            out = 0;
            if (err != ERROR_NONE) {
                err = ERROR_UNSPECIFIC;
            }
        }
        
        out_err = err;
        *length = out;
    }
    
    unlock_dataproxy(proxy);

    return out_err;
}

error_t dataproxy_array_update(dataproxy_t *proxy, XPLMDataTypeID type, void *values, int offset, int count, session_t *source_session) {
    error_t err = ERROR_NONE;
    
    if (!values || offset < 0 || count < 0) {
        return ERROR_UNSPECIFIC;
    }
    
    if (!is_array_type(type)) {
        return DATAPROXY_ERROR_UNSUPPORTED_TYPE;
    }

    if (count == 0) {
        return ERROR_NONE;
    }
    
    err = lock_dataproxy(proxy);
    if (err != ERROR_NONE) {
        return err;
    }

    dataproxy_array_update_f setter = NULL;
    void *operations_ref = NULL;

    if (proxy->state != DATAPROXY_STATE_REGISTERED) {
        err = DATAPROXY_ERROR_INVALID_STATE;
    } else if (!dataproxy_can_write(proxy, source_session)) {
        err = DATAPROXY_ERROR_PERMISSION_DENIED;
    } else if (!proxy->operations.array_update || (proxy->types & type) == 0) {
        err = DATAPROXY_ERROR_UNSUPPORTED_TYPE;
    } else {
        setter = proxy->operations.array_update;
        operations_ref = proxy->operations_ref;
    }
    
    // dataproxy has to be unlocked before calling setter as it will otherwise deadlock
    // FIXME: check if this is sufficiently secured against e.g. concurrent proxy deregistration
    unlock_dataproxy(proxy);

    if (err == ERROR_NONE && setter) {
        err = setter(operations_ref, type, values, offset, count, source_session);
    }

    return err;
}

error_t unregister_dropped_dataproxies(dataproxy_registry_t *registry) {
    error_t err = ERROR_NONE;
    error_t out_err = ERROR_NONE;
    
    RCLOG_TRACE("[dataproxy] unregister_dropped_dataproxies");
       
    err = lock_dataproxy_registry(registry);
    if (err != ERROR_NONE) {
        return err;
    }

    prealloc_list_t *list = list_dataproxies_with_state(registry, DATAPROXY_STATE_DROPPED);
    if (!list) {
        RCLOG_WARN("[dataproxy] unregister_dropped_dataproxies: failed listing");
        unlock_dataproxy_registry(registry);
        return ERROR_MEMORY_ALLOCATION;
    }
    
    prealloc_list_item_t *item = list->first_in_use_item;
    while (item) {
        dataproxy_t *proxy = item->value;
        if (proxy && proxy->state == DATAPROXY_STATE_DROPPED) {
            err = unregister_dataproxy(proxy);
            if (err != ERROR_NONE) {
                RCLOG_WARN("[dataproxy] failed to unregister dropped dataproxy %s (error %d)", proxy->dataref_name, err);
                out_err = ERROR_UNSPECIFIC;
            }
        }
        
        item = item->next_in_use;
    }

    unlock_dataproxy_registry(registry);

    RCLOG_TRACE("[dataproxy] unregister_dropped_dataproxies: done");
    
    return out_err;
}
