#ifndef ARRAYS_H
#define ARRAYS_H

/**
 * @file arrays.h Common data structures for arrays
 *
 * This file defines the following data structures:
 *
 * - @ref CommonDataStructuresDynArray
 */

#include <stdlib.h>
#include <stdbool.h>

/**
 * @addtogroup CommonDataStructuresDynArray Dynamic array
 * This is an implementation of a dynamically resizable array using a continuous chunk of memory that gets reallocated
 * on demand.
 *
 * The array is initialized with a then constant item size which gets used throughout all operations. Current allocation
 * is tracked via item-number based capacity. For example, an array with a capacity of 2 and an item size of 4 bytes has
 * a memory chunk of 2*4=8 bytes allocated. The currently used length may be at most the current capacity. If items get
 * removed, memory allocation does not change; it remains available for later reuse as more items get added again. When
 * additions beyond the current capacity get requested, the whole memory is reallocated and previous contents are
 * copied. If that expansion fails, the previous size and content will be retained.
 *
 * Just changing the length without altering capacity is referred to as *logical resizing*.
 * Reallocating memory to expand capacity is referred to as *physical resizing*.
 *
 * Because the memory is allocated as a single continuous chunk and packed without any spacing/framing, pointers can
 * also be used for standard C array access. However, it is highly recommended to use the provided functions/defines
 * instead of working on the raw dynamic_array_t.
 *
 * Thread-safety has to be ensured when using this data structure.
 *
 * @{
 */

/**
 * Container for a dynamic array.
 */
typedef struct {
    /// current logical length of the array (number of items in use)
    int length;
    /// current physical capacity of the array (number of items allocated)
    int capacity;
    /// the size to allocate for each item
    size_t item_size;
    /// pointer to array data, use functions to access
    void *data;
} dynamic_array_t;

/**
 * Creates a new dynamic array, allocating the initial capacity immediately.
 * @param item_size size of each item in bytes
 * @param initial_capacity number of items to allocate memory for
 * @return the array container; NULL on error
 */
dynamic_array_t* create_dynamic_array(size_t item_size, int initial_capacity);

/**
 * Destroys the given dynamic array, deallocating all memory (items are lost).
 * @param arr dynamic array to destroy
 */
void destroy_dynamic_array(dynamic_array_t *arr);

/**
 * Checks if the array's current capacity is sufficient, otherwise performs memory reallocation to the
 * specified new capacity.
 * @param arr dynamic array to ensure capacity of
 * @param capacity number of items required to be allocated by the array
 * @return true if capacity is sufficient (after possible reallocation); false if capacity is insufficient (memory reallocation failed)
 */
bool dynamic_array_ensure_capacity(dynamic_array_t *arr, int capacity);

/**
 * Returns a pointer to the specified item on the given dynamic array.
 * @param arr dynamic array to access
 * @param index wanted item index (zero-based)
 * @return pointer to the requested item; NULL on error
 */
void* dynamic_array_get_pointer(dynamic_array_t *arr, int index);

/**
 * Copies the data for a single item into the dynamic array.
 * @param arr dynamic array to copy into
 * @param index item index on dynamic array to set data on (zero-based); must be within array length
 * @param data data to be copied; must match single item size
 * @return true if successful, false on error (out of bounds)
 */
bool dynamic_array_set_from_pointer(dynamic_array_t *arr, int index, void *data);

/**
 * Changes the dynamic array's length.
 *
 * Growing an array past current capacity will reallocate the array.
 *
 * Items that were stored beyond the new length are not cleared. Growing an array that was previously shrunk will reveal
 * those items again.
 * @param arr dynamic array to resize
 * @param length number of items after resizing
 * @return true if successful, false on error (capacity could not be increased)
 */
bool dynamic_array_set_length(dynamic_array_t *arr, int length);

/// all items (from start index) are copied when used for count on #dynamic_array_copy_from_other()
#define DYNAMIC_ARRAY_COPY_ALL -12836
/// the destination array is allowed to be physically reallocated to fit the items to be copied
#define DYNAMIC_ARRAY_ALLOW_CAPACITY_CHANGE true
/// the destination array will not be reallocated; instead the operation will fail if insufficient space is available
#define DYNAMIC_ARRAY_DENY_CAPACITY_CHANGE false
/// the destination array is allowed to be logically resized to fit all items to be copied
#define DYNAMIC_ARRAY_ALLOW_LENGTH_CHANGE true
/// the destination array will not be logically resized; instead the operation will fail if length is insufficient
#define DYNAMIC_ARRAY_DENY_LENGTH_CHANGE false

/**
 * Copies a dynamic array onto another one.
 *
 * Source and destination indices can be provided to copy only a part of an array as well as merge with existing items.
 *
 * #DYNAMIC_ARRAY_COPY_ALL can be used on count to copy everything from source index to the last item of the source
 * array.
 *
 * #DYNAMIC_ARRAY_ALLOW_CAPACITY_CHANGE and #DYNAMIC_ARRAY_DENY_CAPACITY_CHANGE decide if the destination array is allowed
 * to be physically resized (reallocated) if needed.
 *
 * #DYNAMIC_ARRAY_ALLOW_LENGTH_CHANGE and #DYNAMIC_ARRAY_DENY_LENGTH_CHANGE decide if the destination array is allowed to
 * be logically resized if needed.
 *
 * If the copy operation fails, the destination array is left unchanged.
 *
 * Thread safety must be ensured over both arrays at the same time.
 *
 * @param dest_arr dynamic array to copy into
 * @param dest_index index where to store the first copied item to on the destination array
 * @param src_arr dynamic array to copy from
 * @param src_index index where to read the first item to be copied from the source array
 * @param count number of items to copy; #DYNAMIC_ARRAY_COPY_ALL can be used if a copy of all (remaining) items is wanted
 * @param allow_capacity_change Is the destination array allowed to be physically resized if needed? Use #DYNAMIC_ARRAY_ALLOW_CAPACITY_CHANGE and #DYNAMIC_ARRAY_DENY_CAPACITY_CHANGE.
 * @param allow_length_change Is the destination array allowed to be logically resized if needed? Use #DYNAMIC_ARRAY_ALLOW_LENGTH_CHANGE and #DYNAMIC_ARRAY_DENY_LENGTH_CHANGE.
 * @return true on success; false if failed
 */
bool dynamic_array_copy_from_other(dynamic_array_t *dest_arr, int dest_index, dynamic_array_t *src_arr, int src_index, int count, bool allow_capacity_change, bool allow_length_change);

// FIXME: dynamic_array_get_item can dereference null; there is no good way to prevent that, consider removal
/// @deprecated unsafe due to null-pointer dereference, do not use in new code
#define dynamic_array_get_item(type, arr, index) (*((type*) dynamic_array_get_pointer(arr, index)))
// TODO: check if still needed (currently unused?)
/// @deprecated unused so far; pending removal
#define dynamic_array_set_item(type, arr, index, value) (dynamic_array_set_from_pointer(arr, index, (void*) &value))

/// @}

#endif
