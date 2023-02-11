#ifndef LISTS_H
#define LISTS_H

#include <stdbool.h>
#include <stdint.h>

#define PREALLOC_LIST_BLOCK_SIZE 5

#define PREALLOC_ITEM_STATUS_PRISTINE 0
#define PREALLOC_ITEM_STATUS_IN_USE 1
#define PREALLOC_ITEM_STATUS_DELETED 2

#define PREALLOC_ITEM_IMMEDIATE_DESTRUCTION false
#define PREALLOC_ITEM_DEFER_DESTRUCTION true

#define PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS false
#define PREALLOC_LIST_OVERRIDE_DEFERRED_DESTRUCTORS true

typedef uint8_t prealloc_item_status_t;

typedef void list_value_destructor_f (void *value);

typedef struct _prealloc_list_item_t prealloc_list_item_t;
typedef struct _prealloc_list_item_t {
    prealloc_item_status_t status;
    void *value;
    list_value_destructor_f *value_destructor;
    prealloc_list_item_t *prev_in_use;
    prealloc_list_item_t *next_in_use;
} prealloc_list_item_t;

typedef struct _prealloc_list_block_t prealloc_list_block_t;
typedef struct _prealloc_list_block_t {
    prealloc_list_block_t *next_block;
    int first_pristine_item_index;
    prealloc_list_item_t items[PREALLOC_LIST_BLOCK_SIZE];
} prealloc_list_block_t;

typedef struct {
    int size;
    prealloc_list_item_t *first_in_use_item;
    prealloc_list_item_t *last_in_use_item;
    
    prealloc_list_block_t *head_block;
    prealloc_list_block_t *tail_block;
} prealloc_list_t;

prealloc_list_t* create_preallocated_list();
void destroy_preallocated_list(prealloc_list_t *list, list_value_destructor_f value_destructor, bool override_deferred_destructors);

bool prealloc_list_append(prealloc_list_t *list, void *value);
prealloc_list_item_t* prealloc_list_get_item(prealloc_list_t *list, int index);
bool prealloc_list_delete_item(prealloc_list_t *list, prealloc_list_item_t *item, list_value_destructor_f value_destructor, bool defer_value_destruction);
bool prealloc_list_compact(prealloc_list_t *list, list_value_destructor_f value_destructor, bool override_deferred_destructors);

#endif
