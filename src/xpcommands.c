#include "logger.h"
#include "utils.h"

#include "xpcommands.h"

#include "xptypes.h"

error_t create_xpcommand_registry(xpcommand_registry_t **registry) {
    *registry = zalloc(sizeof(xpcommand_registry_t));
    if (!(*registry)) {
        return ERROR_MEMORY_ALLOCATION;
    }

    (*registry)->commands = create_list();
    if (!(*registry)->commands) {
        free(*registry);
        *registry = NULL;
        return ERROR_MEMORY_ALLOCATION;
    }

    if (mtx_init(&(*registry)->mutex, mtx_plain | mtx_recursive) != thrd_success) {
        destroy_list((*registry)->commands, free);
        free(*registry);
        *registry = NULL;
        return ERROR_UNSPECIFIC;
    }

    return ERROR_NONE;
}

error_t destroy_xpcommand_registry(xpcommand_registry_t *registry) {
    if (mtx_lock(&registry->mutex) != thrd_success) {
        return ERROR_MUTEX_FAILED;
    }

    if (registry->destruction_pending) {
        mtx_unlock(&registry->mutex);
        return ERROR_DESTRUCTION_PENDING;
    }

    if (registry->commands->size != 0) {
        mtx_unlock(&registry->mutex);
        RCLOG_ERROR("[xpcommands] commands are still registered, unable to destroy registry");
        return ERROR_UNSPECIFIC;
    }
    
    registry->destruction_pending = true;

    // relock so all threads had a chance to see pending destruction
    mtx_unlock(&registry->mutex);
    if (mtx_lock(&registry->mutex) != thrd_success) {
        RCLOG_WARN("[xpcommands] failed to regain lock during destruction, will continue anyway");
    } else {
        mtx_unlock(&registry->mutex);
    }
    
    destroy_list(registry->commands, free);
    mtx_destroy(&registry->mutex);
    free(registry);

    return ERROR_NONE;
}

error_t lock_xpcommand_registry(xpcommand_registry_t *registry) {
    if (!registry) {
        RCLOG_WARN("[xpcommands] lock_xpcommand_registry called with NULL");
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

void unlock_xpcommand_registry(xpcommand_registry_t *registry) {
    if (!registry) {
        RCLOG_WARN("[xpcommands] unlock_xpcommand_registry called with NULL");
        return;
    }
    
    mtx_unlock(&registry->mutex);
}

error_t lock_xpcommand(xpcommand_t *proxy) {
    if (!proxy) {
        RCLOG_WARN("[xpcommands] lock_xpcommand called with NULL");
        return ERROR_UNSPECIFIC;
    }
    
    if ((proxy->state != XPCOMMAND_STATE_CREATED) && (proxy->state != XPCOMMAND_STATE_REGISTERED) && (proxy->state != XPCOMMAND_STATE_DROPPED)) {
        return ERROR_UNSPECIFIC;
    }
    
    return lock_xpcommand_registry(proxy->registry);
}

void unlock_xpcommand(xpcommand_t *proxy) {
    if (!proxy) {
        RCLOG_WARN("[xpcommands] unlock_xpcommand called with NULL");
        return;
    }
    
    unlock_xpcommand_registry(proxy->registry);
}

xpcommand_t* create_xpcommand(xpcommand_registry_t *registry, char *name, char *description, xpcommand_phase_t phase, xpcommand_propagation_t propagate, void *callback_ref, xpcommand_callback_f callback) {
    if (!registry || !name || !description || ((phase != XPCOMMAND_PHASE_BEFORE) && (phase != XPCOMMAND_PHASE_AFTER)) || ((propagate != XPCOMMAND_PROPAGATE_STOP) && (propagate != XPCOMMAND_PROPAGATE_CONTINUE)) || !callback) {
        return NULL;
    }

    xpcommand_t *proxy = zalloc(sizeof(xpcommand_t));
    if (!proxy) {
        return NULL;
    }

    proxy->registry = registry;
    proxy->state = XPCOMMAND_STATE_CREATED;
    proxy->phase = phase;
    proxy->propagate = propagate;
    proxy->callback = callback;
    proxy->callback_ref = callback_ref;

    proxy->name = copy_string(name);
    if (!proxy->name) {
        goto error;
    }
    
    proxy->description = copy_string(description);
    if (description && !proxy->description) {
        goto error;
    }

    if (lock_xpcommand_registry(registry) != ERROR_NONE) {
        goto error;
    }

    if (!list_append(registry->commands, proxy)) {
        unlock_xpcommand_registry(registry);
        goto error;
    }
    
    unlock_xpcommand_registry(registry);

    return proxy;

 error:
    // registry must have been unlocked at this point, command must not have been stored in list
    if (proxy) {
        if (proxy->name) {
            free(proxy->name);
        }

        if (proxy->description) {
            free(proxy->description);
        }
        
        free(proxy);
    }

    return NULL;
}

static int xp_command_handler(XPLMCommandRef inCommand, XPLMCommandPhase inPhase, void *inRefcon) {
    RCLOG_TRACE("[xpcommands] xp_command_handler inCommand=%p, inPhase=%d, inRefcon=%p", inCommand, inPhase, inRefcon);
    
    xpcommand_t *proxy = inRefcon;
    if (!proxy) {
        RCLOG_WARN("[xpcommands] xp_command_handler called without inRefcon");
        return XPCOMMAND_PROPAGATE_CONTINUE;
    }

    if (lock_xpcommand(proxy) != ERROR_NONE) {
        RCLOG_WARN("[xpcommands] xp_command_handler failed to lock");
        return XPCOMMAND_PROPAGATE_CONTINUE;
    }

    if (proxy->state != XPCOMMAND_STATE_REGISTERED) {
        RCLOG_WARN("[xpcommands] xp_command_handler called in bad state (%d): %s", proxy->state, proxy->name);
        unlock_xpcommand(proxy);
        return XPCOMMAND_PROPAGATE_CONTINUE;
    }
    
    if (inCommand != proxy->xp_ref) {
        RCLOG_WARN("[xpcommands] xp_command_handler called with non-matching inRefcon: %s", proxy->name);
        unlock_xpcommand(proxy);
        return XPCOMMAND_PROPAGATE_CONTINUE;
    }

    if (!proxy->callback) {
        RCLOG_WARN("[xpcommands] xp_command_handler no callback: %s", proxy->name);
        unlock_xpcommand(proxy);
        return XPCOMMAND_PROPAGATE_CONTINUE;
    }
    
    RCLOG_TRACE("[xpcommands] xp_command_handler calling into %p with %p for %s", proxy->callback, proxy->callback_ref, proxy->name);
    proxy->callback(proxy->callback_ref, inPhase);
    RCLOG_TRACE("[xpcommands] xp_command_handler call returned");
    
    xpcommand_propagation_t propagate = proxy->propagate;
    
    unlock_xpcommand(proxy);

    return propagate;
}

error_t register_xpcommand(xpcommand_t *proxy) {
    error_t err = ERROR_NONE;

    RCLOG_TRACE("[xpcommands] register_xpcommand will lock");
    err = lock_xpcommand(proxy);
    RCLOG_TRACE("[xpcommands] register_xpcommand locked");
    if (err != ERROR_NONE) {
        return err;
    }

    if (proxy->state != XPCOMMAND_STATE_CREATED) {
        RCLOG_WARN("[xpcommands] register_xpcommand bad state %d", proxy->state);
        unlock_xpcommand(proxy);
        return ERROR_UNSPECIFIC;
    }

    RCLOG_TRACE("[xpcommands] register_xpcommand will call XPLMFindCommand(%s)", proxy->name);
    proxy->xp_ref = XPLMFindCommand(proxy->name);
    if (proxy->xp_ref != NO_XP_COMMAND) {
        RCLOG_DEBUG("[xpcommands] register_xpcommand found existing command %p", proxy->xp_ref);
    } else {
        RCLOG_TRACE("[xpcommands] register_xpcommand not found, will call XPLMCreateCommand(%s, %s)", proxy->name, proxy->description);
        proxy->xp_ref = XPLMCreateCommand(proxy->name, proxy->description);
        if (proxy->xp_ref == NO_XP_COMMAND) {
            RCLOG_WARN("[xpcommands] register_xpcommand command creation failed");
            unlock_xpcommand(proxy);
            return ERROR_UNSPECIFIC;
        }
    }

    RCLOG_TRACE("[xpcommands] register_xpcommand will call XPLMRegisterCommandHandler(%p, %p, %d, %p)", proxy->xp_ref, xp_command_handler, proxy->phase, proxy);
    XPLMRegisterCommandHandler(proxy->xp_ref, xp_command_handler, proxy->phase, proxy);
    RCLOG_TRACE("[xpcommands] register_xpcommand registered");
    proxy->state = XPCOMMAND_STATE_REGISTERED;
    
    unlock_xpcommand(proxy);

    RCLOG_TRACE("[xpcommands] register_xpcommand done");
    
    return ERROR_NONE;
}

static error_t _unregister_destroy_xpcommand(xpcommand_t *proxy, list_item_t *item) {
    if (!item) {
        return ERROR_UNSPECIFIC;
    }

    if ((proxy->state == XPCOMMAND_STATE_REGISTERED) || (proxy->state == XPCOMMAND_STATE_DROPPED)) {
        if (proxy->xp_ref == NO_XP_COMMAND) {
            return ERROR_UNSPECIFIC;
        }
        
        XPLMUnregisterCommandHandler(proxy->xp_ref, xp_command_handler, proxy->phase, proxy);

        proxy->xp_ref = NULL;
    }

    list_delete_item(proxy->registry->commands, item, NULL);
    
    proxy->state = XPCOMMAND_STATE_DESTROYED;

    if (proxy->name) {
        free(proxy->name);
        proxy->name = NULL;
    }

    if (proxy->description) {
        free(proxy->description);
        proxy->description = NULL;
    }

    free(proxy);

    return ERROR_NONE;
}

error_t unregister_destroy_xpcommand(xpcommand_t *proxy) {
    error_t err = ERROR_NONE;
    xpcommand_registry_t *registry = proxy->registry;

    err = lock_xpcommand_registry(registry);
    if (err != ERROR_NONE) {
        return err;
    }

    list_item_t *item = list_find(proxy->registry->commands, proxy);
    err = _unregister_destroy_xpcommand(proxy, item);
    
    unlock_xpcommand_registry(registry);

    return err;
}

error_t unregister_dropped_xpcommands(xpcommand_registry_t *registry) {
    error_t err = ERROR_NONE;
    error_t out_err = ERROR_NONE;

    err = lock_xpcommand_registry(registry);
    if (err != ERROR_NONE) {
        return ERROR_MUTEX_FAILED;
    }
    
    list_item_t *item = registry->commands->head;
    while (item) {
        list_item_t *next = item->next;
        xpcommand_t *proxy = item->value;

        if (proxy->state == XPCOMMAND_STATE_DROPPED) {
            err = _unregister_destroy_xpcommand(proxy, item);
            if (err != ERROR_NONE) {
                RCLOG_WARN("[xpcommands] failed to unregister dropped command (error %d): %s", err, proxy->name);
                out_err = ERROR_UNSPECIFIC;
            }
        }
        
        item = next;
    }

    unlock_xpcommand_registry(registry);

    return out_err;
}

error_t drop_xpcommand(xpcommand_t *proxy) {
    error_t err = ERROR_NONE;
    error_t out_err = ERROR_NONE;

    err = lock_xpcommand(proxy);
    if (err != ERROR_NONE) {
        return err;
    }

    if ((proxy->state == XPCOMMAND_STATE_REGISTERED) || (proxy->state == XPCOMMAND_STATE_CREATED)) {
        proxy->state = XPCOMMAND_STATE_DROPPED;
    } else {
        out_err = ERROR_UNSPECIFIC;
    }
    
    unlock_xpcommand(proxy);

    return out_err;
}
