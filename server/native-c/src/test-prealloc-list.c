#include <stdio.h>
#include <stdlib.h>

#include "lists.h"

static int final_value_destructor_calls = 0;

static void final_value_destructor(void *value) {
    final_value_destructor_calls++;
    free(value);
}

int main(int argc, char **argv) {
    int num_items = 21854;
    if (argc > 1) {
        num_items = atoi(argv[1]);
    }

    printf("creating list\n");
    prealloc_list_t *list = create_preallocated_list();
    if (!list) {
        printf("failed to create list\n");
        return 1;
    }

    printf("inserting %d items (change with first parameter)\n", num_items);
    for (int i=0; i<num_items; i++) {
        int *value = malloc(sizeof(int));
        if (!value) {
            printf("failed to create value\n");
            return 1;
        }
        *value = i;
        
        if (!prealloc_list_append(list, value)) {
            printf("list failed to append item\n");
            return 1;
        }
    }

    if (list->size == num_items) {
        printf("size is correct\n");
    } else {
        printf("list indicates size=%d, expected %d\n", list->size, num_items);
        return 1;
    }
    
    printf("verifying items by direct reference forward\n");
    prealloc_list_item_t *item = list->first_in_use_item;
    int expected_item_value = 0;
    while (item) {
        if (!item->value) {
            printf("value missing\n");
            return 1;
        }

        int item_value = *((int*) item->value);
        if (item_value != expected_item_value) {
            printf("found value %d, expected %d\n", expected_item_value, item_value);
            return 1;
        }

        expected_item_value++;
        
        item = item->next_in_use;
    }
    if (expected_item_value != num_items) {
        printf("incomplete iterations; expected %d, got %d\n", num_items, expected_item_value);
        return 1;
    }
    
    printf("verifying items by direct reference reversed\n");
    item = list->last_in_use_item;
    expected_item_value = num_items-1;
    while (item) {
        if (!item->value) {
            printf("value missing\n");
            return 1;
        }

        int item_value = *((int*) item->value);
        if (item_value != expected_item_value) {
            printf("found value %d, expected %d\n", expected_item_value, item_value);
            return 1;
        }

        expected_item_value--;
        
        item = item->prev_in_use;
    }
    if (expected_item_value >= 0) {
        printf("incomplete iterations; expected %d, got %d left to go\n", num_items, expected_item_value);
        return 1;
    }

    printf("verifying items via index getter\n");
    for (int i=0; i<num_items; i++) {
        item = prealloc_list_get_item(list, i);
        if (!item) {
            printf("index %d not found\n", i);
            return 1;
        }
    }

    printf("checking out of bounds access\n");
    if (prealloc_list_get_item(list, -1)) {
        printf("negative access succeeded but should have been blocked\n");
        return 1;
    }
    
    if (prealloc_list_get_item(list, num_items)) {
        printf("access 1 item past end of list succeeded but should have been blocked\n");
        return 1;
    }
    
    if (prealloc_list_get_item(list, num_items+1)) {
        printf("access 2 items past end of list succeeded but should have been blocked\n");
        return 1;
    }
    
    int expected_final_value_destructor_calls = num_items;

    // TODO: delete every n-th item deferred
    // TODO: verify number of destructor calls is 0
    // TODO: verify for expected list size
    // TODO: verify expected items are present, found all 3 ways

    // TODO: delete every m-th item immediately
    // TODO: verify expected number of destructor calls
    // TODO: verify expected items are present, found all 3 ways
    
    // TODO: delete first element
    // TODO: verify new first element is correct
    // TODO: check links are correct

    // TODO: compact using deferred destructor calls
    // TODO: verify number of deferred destructor calls is correct
    // TODO: verify number of immediate destrictor calls is correct 
    
    // TODO: verify expected items are present, found all 3 ways
    // TODO: delete some item deferred
    
    destroy_preallocated_list(list, final_value_destructor, PREALLOC_LIST_CALL_DEFERRED_DESTRUCTORS);

    if (final_value_destructor_calls != expected_final_value_destructor_calls) {
        printf("final value destructor was called %d times, expected %d\n", final_value_destructor_calls, expected_final_value_destructor_calls);
        return 1;
    }

    // TODO: verify number of deferred destructor calls

    printf("done\n");
    
    return 0;
}
