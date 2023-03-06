#include <string.h>

#include "arrays.h"

dynamic_array_t* create_dynamic_array(size_t item_size, int initial_capacity) {
    if (item_size < 1 || initial_capacity < 0) {
        return NULL;
    }

    dynamic_array_t *arr = malloc(sizeof(dynamic_array_t));
    if (!arr) {
        return NULL;
    }
    
    memset(arr, 0, sizeof(dynamic_array_t));

    arr->capacity = initial_capacity;
    arr->item_size = item_size;
    
    return arr;
}

void destroy_dynamic_array(dynamic_array_t *arr) {
    if (!arr) {
        return;
    }

    if (arr->data) {
        free(arr->data);
    }
    
    free(arr);
}

void* dynamic_array_get_pointer(dynamic_array_t *arr, int index) {
    if (index < 0 || index >= arr->length) {
        // out of bounds
        return NULL;
    }

    return arr->data + (index * arr->item_size);
}

bool dynamic_array_ensure_capacity(dynamic_array_t *arr, int capacity) {
    if (!arr || capacity < 0) {
        return false;
    }

    if (capacity <= arr->capacity) {
        // requested capacity is already covered
        return true;
    }

    // we need to "grow" the array; i.e. request larger memory segment and copy existing data
    size_t old_size = arr->capacity * arr->item_size;
    size_t new_size = capacity * arr->item_size;
    void *new_data = malloc(new_size);
    if (!new_data) {
        return false;
    }

    if (old_size) {
        memcpy(new_data, arr->data, old_size);
    }
    
    memset(new_data + old_size, 0, new_size - old_size);

    if (arr->data) {
        free(arr->data);
    }

    arr->data = new_data;
    arr->capacity = capacity;

    return true;
}

bool dynamic_array_set_length(dynamic_array_t *arr, int length) {
    if (!dynamic_array_ensure_capacity(arr, length)) {
        return false;
    }

    arr->length = length;

    return true;
}

bool dynamic_array_set_from_pointer(dynamic_array_t *arr, int index, void *data) {
    if (!data) {
        return false;
    }

    void *dest = dynamic_array_get_pointer(arr, index);
    if (!dest) {
        return false;
    }
    
    memcpy(dest, data, arr->item_size);

    return true;
}
