#ifndef HASHMAP_H
#define HASHMAP_H

/**
 * @file hashmap.h
 *
 * This file defines the following data structures:
 *
 * - @ref CommonDataStructuresHashMap
 */

/**
 * @addtogroup CommonDataStructuresHashMap Hash map (string-based)
 *
 * A hash map stores a set of key-value pairs indexed by their (string) key. Data storage happens in two stages:
 * The first stage is an array of all possible key hashes that holds pointers to single-linked lists that, on second
 * stage, contains all key-value pairs whose keys lead to the same hash. All items are unordered.
 *
 * NULL values cannot be distinguished from look-up failures.
 *
 * Thread-safety has to be ensured by callers.
 *
 * @{
 */

#include <stdbool.h>
#include <stdint.h>

#include "lists.h"

/// the number of bytes used for a hash result (hash length/size)
#define HASH_BYTES 2
/// the number of all possible combinations resulting from hashing
#define HASH_COMBINATIONS (1 << (HASH_BYTES * 8))

/**
 * Destroys the value of a hash map item; the key is only provided for optional reference and must not be freed.
 * @param key key of the hash map item holding the value to be destroyed; the key must not be freed
 * @param value the value to be destroyed; must be freed
 */
typedef void hashmap_value_destructor_f (char *key, void *value);

typedef struct _hashmap_item_t hashmap_item_t;
/// an item linked in a hash map
typedef struct _hashmap_item_t {
    /// null-terminated string used to register the value on the map
    char *key;
    /// the value associated with the key
    void *value;
    /// pointer to another item whose key has the same hash; NULL if no more collisions
    hashmap_item_t *next;
} hashmap_item_t;

/// an unordered string-based hash map
typedef struct {
    /// linked lists of items indexed by hash
    hashmap_item_t *items[HASH_COMBINATIONS];
} hashmap_t;

/**
 * Creates a new hash map.
 * @return hash map instance; NULL on erorr
 */
hashmap_t* create_hashmap();
/**
 * Destroys a hash map.
 * @param map hash map to be destroyed
 * @param value_destructor will be called to destroy each value still present in the hash map
 */
void destroy_hashmap(hashmap_t *map, hashmap_value_destructor_f value_destructor);

/**
 * Retrieves the value associated with the specified key on the given hash map.
 * @param map hash map to perform look-up on
 * @param key key to look up value for; null-terminated string; must not be NULL
 * @return value reference associated with the key; NULL if not found (cannot be distinguished from stored NULL values)
 */
void* hashmap_get(hashmap_t *map, char *key);
/**
 * Associates the given value reference with the specified key.
 * @param map hash map to store value on
 * @param key key to store value for; null-terminated string; must not be NULL
 * @param value value reference to store; storing NULL is possible but cannot be distinguished from non-existing entries
 * @param old_value the previously stored, now overridden value reference; NULL if no value was set (or reference was NULL)
 * @return true if stored successfully, false on error
 */
bool hashmap_put(hashmap_t *map, char *key, void *value, void **old_value);

/**
 * Checks if the given hashmap is empty, i.e. if it holds no entries.
 * @param map hashmap to be checked; may be NULL
 * @return true if map is empty or NULL; false if it holds at least one entry
 */
bool is_hashmap_empty(hashmap_t *map);

/**
 * Returns a list containing references to all keys currently present in the given hashmap.
 *
 * Memory management:
 * - The returned list is to be managed by the caller.
 * - List values (map keys) are the original pointers contained within the map and must NOT be freed or altered.
 *
 * Concurrency:
 * - Map must not be modified concurrently while this function is being executed.
 * - If the map gets modified concurrently, memory referenced by the returned list may become invalid. Locks must be
 *   ensured by caller, if needed.
 *
 * @param map hashmap to retrieve keys of
 * @return all keys currently present in map as shared pointers (must not be modified/freed); NULL on error
 */
list_t* hashmap_reference_keys(hashmap_t *map);

/**
 * Returns a list containing copies of all keys currently present in the given hashmap.
 *
 * Memory management:
 * - The returned list is to be managed by the caller.
 * - List values (map keys) are copies to be managed by the caller.
 *
 * Concurrency:
 * - Map must not be modified concurrently while this function is being executed.
 *
 * @param map hashmap to retrieve keys of
 * @return copies of all keys currently present in map (must be freed when no longer needed); NULL on error
 */
list_t* hashmap_copy_keys(hashmap_t *map);
/// @}

#endif
