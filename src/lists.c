#include <stdlib.h>
#include <string.h>

#include "lists.h"
#include "utils.h"

static prealloc_list_block_t* create_preallocated_list_block() {
    prealloc_list_block_t *block = malloc(sizeof(prealloc_list_block_t));
    if (!block) {
        return NULL;
    }

    memset(block, 0, sizeof(prealloc_list_block_t));

    return block;
}

prealloc_list_t* create_preallocated_list() {
    prealloc_list_t *list = malloc(sizeof(prealloc_list_t));
    if (!list) {
        return NULL;
    }

    memset(list, 0, sizeof(prealloc_list_t));

    prealloc_list_block_t *block = create_preallocated_list_block();
    if (!block) {
        free(list);
        return NULL;
    }

    list->head_block = block;
    list->tail_block = block;

    return list;
}

void destroy_preallocated_list(prealloc_list_t *list, list_value_destructor_f value_destructor, bool override_deferred_destructors) {
    prealloc_list_block_t *block = list->head_block;

    while (block) {
        for (int i=0; i<PREALLOC_LIST_BLOCK_SIZE; i++) {
            prealloc_list_item_t *item = &(block->items[i]);
            if (!item->value) {
                continue;
            }

            if (item->value_destructor && (PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS == override_deferred_destructors)) {
                item->value_destructor(item->value);
            } else if (value_destructor) {
                value_destructor(item->value);
            }
        }

        prealloc_list_block_t *next_block = block->next_block;
        free(block);
        
        block = next_block;
    }
    
    free(list);
}

bool prealloc_list_append(prealloc_list_t *list, void *value) {
    prealloc_list_block_t *block = list->tail_block;
    if (block->first_pristine_item_index >= PREALLOC_LIST_BLOCK_SIZE) {
        // last block is full, we need to allocate a new one
        block = create_preallocated_list_block();
        if (!block) {
            return false;
        }
        
        list->tail_block->next_block = block;
        list->tail_block = block;
    }

    prealloc_list_item_t *item = &(block->items[block->first_pristine_item_index]);
    block->first_pristine_item_index++;

    item->status = PREALLOC_ITEM_STATUS_IN_USE;
    item->value = value;
    
    item->prev_in_use = list->last_in_use_item;
    list->last_in_use_item = item;
    
    if (!list->first_in_use_item) {
        list->first_in_use_item = item;
    }

    if (item->prev_in_use) { 
        item->prev_in_use->next_in_use = item;
    }
    
    list->size++;

    return true;
}

prealloc_list_item_t* prealloc_list_get_item(prealloc_list_t *list, int index) {
    if (index < 0 || index >= list->size) {
        return NULL;
    }

    prealloc_list_item_t *item = list->first_in_use_item;
    while (index && item) {
        item = item->next_in_use;
        index--;
    }

    return item;
}

bool prealloc_list_delete_item(prealloc_list_t *list, prealloc_list_item_t *item, list_value_destructor_f value_destructor, bool defer_value_destruction) {
    if (item->status != PREALLOC_ITEM_STATUS_IN_USE) {
        return false;
    }

    list->dirty = true;
    list->size--;
    item->status = PREALLOC_ITEM_STATUS_DELETED;

    if (item == list->first_in_use_item) {
        list->first_in_use_item = item->next_in_use;
    }

    if (item == list->last_in_use_item) {
        list->last_in_use_item = item->prev_in_use;
    }

    if (item->prev_in_use) {
        item->prev_in_use->next_in_use = item->next_in_use;
    }
    
    if (item->next_in_use) {
        item->next_in_use->prev_in_use = item->prev_in_use;
    }
    
    if (defer_value_destruction == PREALLOC_ITEM_DEFER_DESTRUCTION) {
        item->value_destructor = value_destructor;
    } else {
        if (value_destructor) {
            value_destructor(item->value);
        }
        
        item->value = NULL;
    }

    return true;
}

bool prealloc_list_compact(prealloc_list_t *list, list_value_destructor_f value_destructor, bool override_deferred_destructors) {
    if (!list->dirty) {
        // there is nothing to compact, avoid useless copy operations
        return true;
    }
    
    prealloc_list_block_t *new_head_block = create_preallocated_list_block();
    if (!new_head_block) {
        return false;
    }

    // copy all items from old structure to new blocks
    prealloc_list_block_t *new_block = new_head_block;
    prealloc_list_item_t *old_item = list->first_in_use_item;
    prealloc_list_item_t *prev_new_item = NULL;
    while (old_item) {
        if (new_block->first_pristine_item_index >= PREALLOC_LIST_BLOCK_SIZE) {
            // block is full, we need to allocate a new one
            prealloc_list_block_t *new_new_block = create_preallocated_list_block();
            if (!new_new_block) {
                // we keep the old structures; free all previously allocated new blocks again
                new_block = new_head_block;
                while (new_block) {
                    prealloc_list_block_t *next_block = new_block->next_block;
                    free(new_block);
                    new_block = next_block;
                }
                
                return false;
            }

            new_block->next_block = new_new_block;
            new_block = new_new_block;
        }

        prealloc_list_item_t *new_item = &(new_block->items[new_block->first_pristine_item_index]);
        new_block->first_pristine_item_index++;
        
        new_item->status = PREALLOC_ITEM_STATUS_IN_USE;
        new_item->value = old_item->value;

        new_item->prev_in_use = prev_new_item;
        if (prev_new_item) {
            prev_new_item->next_in_use = new_item;
        }
        prev_new_item = new_item;

        old_item = old_item->next_in_use;
    }

    // set new blocks active
    prealloc_list_block_t *old_block = list->head_block;
    list->head_block = new_head_block;
    list->tail_block = new_block;
    
    if (list->size > 0) {
        // all items are consecutive and in use so we can simply link first and last block items
        list->first_in_use_item = &(list->head_block->items[0]);
        list->last_in_use_item = &(new_block->items[new_block->first_pristine_item_index - 1]);
    }

    list->dirty = false;

    // free old blocks
    while (old_block) {
        // destroy item values that have not been taken over
        for (int i=0; i<PREALLOC_LIST_BLOCK_SIZE; i++) {
            prealloc_list_item_t *item = &(old_block->items[i]);
            if (!item->value || item->status != PREALLOC_ITEM_STATUS_DELETED) {
                continue;
            }

            if (item->value_destructor && (PREALLOC_LIST_OVERRIDE_DEFERRED_DESTRUCTORS != override_deferred_destructors)) {
                item->value_destructor(item->value);
            } else if (value_destructor) {
                value_destructor(item->value);
            }
        }

        prealloc_list_block_t *next_block = old_block->next_block;
        free(old_block);
        old_block = next_block;
    }

    return true;
}

list_t* create_list() {
    return zalloc(sizeof(list_t));
}

void destroy_list(list_t *list, list_value_destructor_f value_destructor) {
    list_item_t *item = list->head;
    while (item) {
        if (item->value && value_destructor) {
            value_destructor(item->value);
        }
        
        list_item_t *next = item->next;
        free(item);
        item = next;
    }

    free(list);
}

bool list_append(list_t *list, void *value) {
    list_item_t *item = zalloc(sizeof(list_item_t));
    if (!item) {
        return false;
    }
    
    item->value = value;
    item->prev = list->tail;
    list->tail = item;

    if (item->prev) {
        item->prev->next = item;
    }

    if (!list->head) {
        list->head = item;
    }

    list->size++;

    return true;
}

list_item_t* list_find(list_t *list, void *value) {
    list_item_t *item = list->head;
    while (item) {
        if (item->value == value) {
            return item;
        }
        item = item->next;
    }
    return NULL;
}

void list_delete_item(list_t *list, list_item_t *item, list_value_destructor_f value_destructor) {
    if (item->prev) {
        item->prev->next = item->next;
    }

    if (item->next) {
        item->next->prev = item->prev;
    }

    if (list->head == item) {
        list->head = item->next;
    }

    if (list->tail == item) {
        list->tail = item->prev;
    }

    list->size--;
    
    if (item->value && value_destructor) {
        value_destructor(item->value);
    }

    free(item);
}
