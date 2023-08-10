#ifndef LISTS_H
#define LISTS_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @file lists.h Common data structures for lists
 *
 * This file defines the following data structures:
 *
 * - @ref CommonDataStructuresPreallocList
 * - @ref CommonDataStructuresList
 */

/**
 * @addtogroup CommonDataStructuresPreallocList Structurally pre-allocated list
 * The structurally pre-allocated lists (#prealloc_list_t) provided by this module reserve memory for multiple list
 * items in advance, grouped into blocks (#prealloc_list_block_t) holding #PREALLOC_LIST_BLOCK_SIZE items each.
 *
 * Each #prealloc_list_item_t starts as #PREALLOC_ITEM_STATUS_PRISTINE which marks it as still unused. As values are
 * being appended to the list (#prealloc_list_append()), the first pristine item gets the value assigned and is then
 * marked #PREALLOC_ITEM_STATUS_IN_USE. Items that are in use exist logically in the list; they can be accessed via
 * #prealloc_list_get_item(). When an item gets deleted (#prealloc_list_delete_item()) it still remains physically
 * in the list but ceases to logically exist as it gets skipped by index lookups. Actual deletion only happens if the
 * list is destroyed as a whole (#destroy_preallocated_list()) or if it is being compacted (#prealloc_list_compact).
 *
 * Logically existing ("in use") items can be traversed using any existing item as a starting point or by following the
 * #prealloc_list_t.first_in_use_item and #prealloc_list_t.last_in_use_item pointers. All logical items are
 * double-linked through #prealloc_list_item_t.next_in_use and #prealloc_list_item_t.prev_in_use.
 *
 * The list is extended by new blocks whenever a new value should be appended but no #PREALLOC_ITEM_STATUS_PRISTINE
 * items exist.
 *
 * Deleting items just "punches holes" into the pre-allocated list. While that does not have a negative effect on
 * access times, it does leave garbage behind in memory, especially when combined with deferred value destruction which
 * just records a value's destructor during #prealloc_list_delete_item() but postpones actual invocation. After some
 * use, the list will eventually need to be compacted (#prealloc_list_compact()) which constructs new continuous
 * blocks with all #PREALLOC_ITEM_STATUS_DELETED items removed, calling deferred value destructors as needed.
 *
 * Such lists are mainly useful to keep memory (de)allocation at an absolute minimum, for example during time-critical
 * sections. Compaction should be performed when it is unlikely that a time-critical condition occurs.
 *
 * Concurrent access will lead to structural corruption. Thread-safety for all data access has to be ensured by the
 * caller.
 *
 * @{
 */

/**
 * The number of items each block of a structurally pre-allocated list holds.
 */
#define PREALLOC_LIST_BLOCK_SIZE 5

/**
 * The item has not been used yet (pre-allocated).
 */
#define PREALLOC_ITEM_STATUS_PRISTINE 0

/**
 * The item is currently used by the list, meaning it exists with an index and may have a value.
 */
#define PREALLOC_ITEM_STATUS_IN_USE 1

/**
 * The item has been deleted from the list. Structure is pending reorganization (list is marked dirty). The value
 * held by the item may get destroyed deferred as well (depending on item and action configuration).
 */
#define PREALLOC_ITEM_STATUS_DELETED 2

/**
 * Causes #prealloc_list_delete_item() to immediately call the given value destructor.
 */
#define PREALLOC_ITEM_IMMEDIATE_DESTRUCTION false

/**
 * Causes #prealloc_list_delete_item() to record the given value destructor for deferred call during
 * #destroy_preallocated_list() or #prealloc_list_compact().
 */
#define PREALLOC_ITEM_DEFER_DESTRUCTION true

/**
 * If a deferred destructor was provided to #prealloc_list_delete_item(), that one is actually used instead of the
 * destructor passed to #destroy_preallocated_list() / #prealloc_list_compact().
 */
#define PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS false

/**
 * The destructor passed to #destroy_preallocated_list() / #prealloc_list_compact() is used even if a specific value
 * destructor was queued for deferred execution during #prealloc_list_delete_item().
 */
#define PREALLOC_LIST_OVERRIDE_DEFERRED_DESTRUCTORS true

/// the status of an item in a pre-allocated list; see PREALLOC_ITEM_STATUS_*
typedef uint8_t prealloc_item_status_t;

/**
 * Destroys the value whose reference is passed in.
 *
 * In the easiest case, this might just be a reference to the C library's #free() function but it also allows for
 * more complex destruction of sub-structures.
 *
 * @param value reference to the value to be destroyed
 */
typedef void list_value_destructor_f (void *value);

typedef struct _prealloc_list_item_t prealloc_list_item_t;

/**
 * A single item of a structurally pre-allocated list.
 */
typedef struct _prealloc_list_item_t {
    /// the status of this item; see PREALLOC_ITEM_STATUS_ constants
    prealloc_item_status_t status;
    /// reference to this item's value; may be NULL even if item is in use
    void *value;
    /// value destructor for deferred call; may be NULL
    list_value_destructor_f *value_destructor;
    /// link to previous #PREALLOC_ITEM_STATUS_IN_USE item; NULL if start of logical list
    prealloc_list_item_t *prev_in_use;
    /// link to next #PREALLOC_ITEM_STATUS_IN_USE item; NULL if end of logical list
    prealloc_list_item_t *next_in_use;
} prealloc_list_item_t;

typedef struct _prealloc_list_block_t prealloc_list_block_t;

/**
 * A block holding the configured number of items for a structurally pre-allocated list.
 */
typedef struct _prealloc_list_block_t {
    /// link to the next physical block after this one; NULL if this is the last block currently allocated
    prealloc_list_block_t *next_block;
    /// the first physical index of this block's items array that is #PREALLOC_ITEM_STATUS_PRISTINE and thus free for value assignment
    int first_pristine_item_index;
    /// the physical items pre-allocated by this block
    prealloc_list_item_t items[PREALLOC_LIST_BLOCK_SIZE];
} prealloc_list_block_t;

/**
 * The root element of a structurally pre-allocated list, tracking the overall state and references.
 */
typedef struct {
    /// current size of list, counting all #PREALLOC_ITEM_STATUS_IN_USE items
    int size;
    /// if true then the list contains deleted items and compaction could be useful; false if compaction could not improve anything
    bool dirty;

    /// reference to the first logical item currently #PREALLOC_ITEM_STATUS_IN_USE
    prealloc_list_item_t *first_in_use_item;
    /// reference to the last logical item currently #PREALLOC_ITEM_STATUS_IN_USE
    prealloc_list_item_t *last_in_use_item;

    /// reference to the first physical block of this list
    prealloc_list_block_t *head_block;
    /// reference to the last physical block of this list
    prealloc_list_block_t *tail_block;
} prealloc_list_t;

/**
 * Creates a new structurally pre-allocated list.
 * @return list root; NULL on error
 */
prealloc_list_t* create_preallocated_list();

/**
 * Destroys the given structurally pre-allocated list.
 *
 * In addition to the list structure itself (root and all items) a destructor function can be provided which
 * will be called with each item value that is not NULL. override_deferred_destructors controls if that destructor
 * is also used in case an item already has a (different) deferred value destructor recorded.
 *
 * If no value destruction is wanted (values are memory-managed outside the list), NULL can be provided to disable
 * destructor calls. To fully disable value destruction, override_deferred_destructors would also need to be set to
 * #PREALLOC_LIST_OVERRIDE_DEFERRED_DESTRUCTORS.
 *
 * @param list list to destroy
 * @param value_destructor destructor to be called for non-null values if no deferred destructor was recorded or this destructor is enforced; may be NULL
 * @param override_deferred_destructors Should the value destructor provided to this function be used unconditionally or only if no deferred destructor was recorded? Use #PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS and #PREALLOC_LIST_OVERRIDE_DEFERRED_DESTRUCTORS constants.
 */
void destroy_preallocated_list(prealloc_list_t *list, list_value_destructor_f value_destructor, bool override_deferred_destructors);

/**
 * Appends a value to the end of the given structurally pre-allocated list.
 *
 * The list is extended with a new block if necessary. Callers must ensure thread-safety as needed.
 *
 * @param list list to append to; must not be NULL
 * @param value value reference to be appended at end of list
 * @return true if an item holding the value reference was added, false in case of an error
 */
bool prealloc_list_append(prealloc_list_t *list, void *value);

/**
 * Returns the item at specified index from the given structurally pre-allocated list.
 *
 * Callers must ensure thread-safety as needed.
 *
 * @param list list to retrieve the item from; must not be NULL
 * @param index zero-based index of the item to be retrieved
 * @return list item at specified index; NULL if index is out of bounds
 */
prealloc_list_item_t* prealloc_list_get_item(prealloc_list_t *list, int index);

/**
 * Marks the item as deleted and destroys its value.
 *
 * If a value_destructor has been provided, it will be called with the item value reference if it is not NULL.
 * Value destruction can either be performed instantly or deferred for later when the list is either compacted or
 * destroyed as a whole. However, both compaction and list destruction may choose to override the deferred value
 * destructor provided here. This also includes the decision not to invoke any value destructor at all.
 *
 * Deletion will fail if the item has an invalid status.
 *
 * Callers must ensure thread-safety as needed.
 *
 * @param list list to delete the item from; must contain the item (no check is performed)
 * @param item item to be deleted; must not be NULL
 * @param value_destructor destructor to be called if the value is not NULL; may be NULL
 * @param defer_value_destruction controls when to call the value destructor, if provided; use #PREALLOC_ITEM_IMMEDIATE_DESTRUCTION and #PREALLOC_ITEM_DEFER_DESTRUCTION constants
 * @return true on success; false on error
 */
bool prealloc_list_delete_item(prealloc_list_t *list, prealloc_list_item_t *item, list_value_destructor_f value_destructor, bool defer_value_destruction);

/**
 * Compacts the given structurally pre-allocated list.
 *
 * The list is fully reorganized: All blocks will be recreated from scratch, taking over only items which have not been
 * marked as deleted. Callers must ensure thread-safety as needed.
 *
 * Compaction includes destroying values for items which are still pending deletion. A destructor function can be
 * provided which will be called with each item value to be deleted. override_deferred_destructors controls if that
 * destructor is also used in case an item already has a (different) deferred value destructor recorded.
 *
 * If no value destruction is wanted (values are memory-managed outside the list), NULL can be provided to disable
 * destructor calls. To fully disable value destruction, override_deferred_destructors would also need to be set to
 * #PREALLOC_LIST_OVERRIDE_DEFERRED_DESTRUCTORS.
 *
 * @param list list to compact
 * @param value_destructor destructor to be called for non-null values of to be deleted items if no deferred destructor was recorded or this destructor is enforced; may be NULL
 * @param override_deferred_destructors Should the value destructor provided to this function be used unconditionally or only if no deferred destructor was recorded? Use #PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS and #PREALLOC_LIST_OVERRIDE_DEFERRED_DESTRUCTORS constants.
 */
bool prealloc_list_compact(prealloc_list_t *list, list_value_destructor_f value_destructor, bool override_deferred_destructors);

/**
 * @}
 * @addtogroup CommonDataStructuresList Double-linked and tracking list
 *
 * This is just an ordinary double-linked list structure with an additional root element #list_t to provide direct
 * access to the first and last element of the list and to quickly check the list size. Items (#list_item_t) are
 * immediately allocated and destroyed as values are added to or deleted from the list.
 *
 * Traversal is possible by directly accessing the respective pointers on the data structures.
 *
 * Concurrent access will lead to structural corruption. Thread-safety for all data access has to be ensured by the
 * caller.
 *
 * @{
 */
typedef struct _list_item_t list_item_t;

/**
 * An item of a double-linked list.
 */
typedef struct _list_item_t {
    /// Pointer to the value organized at the position of this list item.
    void *value;

    /// Reference to previous element in list; NULL if head of list.
    list_item_t *prev;

    /// Reference to next element in list; NULL if tail of list.
    list_item_t *next;
} list_item_t;

/**
 * The root of a double-linked list structure, tracking head, tail and size to avoid expensive loops on lookup.
 */
typedef struct {
    /// number of items currently linked in this list
    int size;

    /// first item (start) of this list
    list_item_t *head;

    /// last item (end) of this list
    list_item_t *tail;
} list_t;

/**
 * Creates a new double-linked tracking list structure.
 *
 * @return list root; NULL on error
 */
list_t* create_list();

/**
 * Destroys the given double-linked tracking list structure.
 *
 * In addition to the list structure itself (root and all items) a destructor function can be provided which
 * will be called with each item value that is not NULL. If no value destruction is wanted (values are memory-managed
 * outside the list), NULL can be provided to disable destructor calls.
 *
 * @param list list to be destroyed; must not be NULL
 * @param value_destructor optional destructor to invoke for every item value; set to NULL if not wanted
 */
void destroy_list(list_t *list, list_value_destructor_f value_destructor);

/**
 * Appends a value to the end of the given list.
 *
 * The list root will be updated in terms of head, tail and size. Callers must ensure thread-safety as needed.
 *
 * @param list list to append to; must not be NULL
 * @param value value reference to be appended at end of list
 * @return true if an item holding the value reference was added, false in case of an error
 */
bool list_append(list_t *list, void *value);

/**
 * Searches the given list for the first item holding the specified value reference (same pointer address).
 *
 * Callers must ensure thread-safety as needed.
 *
 * @param list list to search in; must not be NULL
 * @param value value reference to find the item for
 * @return the first item holding the specified value reference; NULL if not found
 */
list_item_t* list_find(list_t *list, void *value);

/**
 * Deletes the given item from the list and destroys the item structure.
 *
 * In addition to the item structure itself a destructor function can be provided which will be called with the item's
 * value if it is not NULL. If no value destruction is wanted (the value is memory-managed outside the list), NULL can
 * be provided to disable destructor calls.
 *
 * The item must be part of the list, otherwise list root structure will be corrupted (no sanity checks are performed).
 *
 * The list root will be updated in terms of head, tail and size. Callers must ensure thread-safety as needed.
 *
 * @param list list to remove the item from; must contain the item
 * @param item item to remove; must not be NULL
 * @param value_destructor optional destructor to invoke for the item's value; set to NULL if not wanted
 */
void list_delete_item(list_t *list, list_item_t *item, list_value_destructor_f value_destructor);

/// @}

#endif
