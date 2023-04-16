#ifndef ARRAYS_H
#define ARRAYS_H

#include <stdlib.h>
#include <stdbool.h>

typedef struct {
    int length;
    int capacity;
    size_t item_size;
    void *data;
} dynamic_array_t;

dynamic_array_t* create_dynamic_array(size_t item_size, int initial_capacity);
void destroy_dynamic_array(dynamic_array_t *arr);

bool dynamic_array_ensure_capacity(dynamic_array_t *arr, int capacity);

void* dynamic_array_get_pointer(dynamic_array_t *arr, int index);
bool dynamic_array_set_from_pointer(dynamic_array_t *arr, int index, void *data);
bool dynamic_array_set_length(dynamic_array_t *arr, int length);

#define DYNAMIC_ARRAY_COPY_ALL -12836
#define DYNAMIC_ARRAY_ALLOW_CAPACITY_CHANGE true
#define DYNAMIC_ARRAY_DENY_CAPACITY_CHANGE false
#define DYNAMIC_ARRAY_ALLOW_LENGTH_CHANGE true
#define DYNAMIC_ARRAY_DENY_LENGTH_CHANGE false

bool dynamic_array_copy_from_other(dynamic_array_t *dest_arr, int dest_index, dynamic_array_t *src_arr, int src_index, int count, bool allow_capacity_change, bool allow_length_change);

#define dynamic_array_get_item(type, arr, index) (*((type*) dynamic_array_get_pointer(arr, index)))
#define dynamic_array_set_item(type, arr, index, value) (dynamic_array_set_from_pointer(arr, index, (void*) &value))

#endif
