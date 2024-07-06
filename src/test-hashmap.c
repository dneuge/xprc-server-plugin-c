#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "hashmap.h"

typedef struct _expected_item_t expected_item_t;
typedef struct _expected_item_t {
    char *key;
    void *value;
    bool overridden;
    expected_item_t *next;
} expected_item_t;

static int compute_random(int min, int max_exclusive) {
    int r = round((double) rand() / RAND_MAX * (max_exclusive - min)) + min;
    if (r < min) {
        r = min;
    } else if (r >= max_exclusive) {
        r = max_exclusive - 1;
    }

    return r;
}

static char* create_random_string(int min_length, int max_length, char min_char, char max_char) {
    int length = compute_random(min_length, max_length + 1);

    char *s = malloc(length+1);
    if (!s) {
        return NULL;
    }
    
    memset(s, 0, length+1);
    
    for (int i=0; i<length; i++) {
        char ch = (char) compute_random(min_char, max_char + 1);
        s[i] = ch;
    }

    return s;
}

static int num_destroy_value_calls = 0;
static void destroy_value(char *key, void *value) {
    num_destroy_value_calls++;
    free(value);
}

static expected_item_t* create_expected_item(char *key, void *value) {
    expected_item_t *item = malloc(sizeof(expected_item_t));
    if (!item) {
        return NULL;
    }
    
    memset(item, 0, sizeof(expected_item_t));
    
    item->key = key;
    item->value = value;

    return item;
}

static void destroy_expected_items(expected_item_t *item) {
    while (item) {
        expected_item_t *next = item->next;

        if (item->overridden) {
            // overridden items should not have resulted in value destructor calls,
            // so the value is still allocated
            free(item->value);
        }
        
        free(item->key);
        free(item);
        item = next;
    }
}

int main(int argc, char **argv) {
    srand(time(NULL));

    printf("--- hashmap tests starting\n");

    int num_items = 1000;
    if (argc > 1) {
        num_items = atoi(argv[1]);
    }
    
    int min_key_length = 10;
    if (argc > 2) {
        min_key_length = atoi(argv[2]);
    }
    
    int max_key_length = 80;
    if (argc > 3) {
        max_key_length = atoi(argv[3]);
    }
    
    hashmap_t *map = create_hashmap();
    if (!map) {
        printf("failed to create map\n");
        return 1;
    }

    printf("inserting %d items to map (change with first parameter), key length between %d and %d (change with second and third parameter)\n", num_items, min_key_length, max_key_length);
    expected_item_t *expected_items_head = NULL;
    expected_item_t **expected_items_reference = &expected_items_head;
    int expected_num_map_entries = 0;
    for (int i=0; i<num_items; i++) {
        char *key = create_random_string(min_key_length, max_key_length, 0x20, 0x7E);
        if (!key) {
            printf("failed to generate key\n");
            return 1;
        }

        int *value = malloc(sizeof(int));
        if (!value) {
            printf("failed to allocate memory to store value\n");
            return 1;
        }
        *value = i;

        expected_item_t *expected_item = create_expected_item(key, value);
        if (!expected_item) {
            printf("failed to create expected item\n");
            return 1;
        }

        *expected_items_reference = expected_item;
        expected_items_reference = &(expected_item->next);

        int *old_value = NULL;
        if (!hashmap_put(map, key, value, (void*) &old_value)) {
            printf("put failed\n");
            return 1;
        }

        expected_item_t *just_added_item = expected_item; // just an alias to make code more readable
        if (!old_value) {
            // no key collision, number of entries in map is supposed to have grown by 1
            expected_num_map_entries++;
        } else {
            // key collision needs to be verified and remembered on expected_item structure
            printf("verifying key collision... ");
            fflush(stdout);

            expected_item_t *collided_expected_item = NULL;
            expected_item_t *item = expected_items_head;
            while (item) {
                if (!item->overridden && (item != just_added_item) && !strcmp(item->key, key)) {
                    if (collided_expected_item) {
                        printf("NOK\nmore collisions for a key were expected\n");
                        return 1;
                    }
                    
                    collided_expected_item = item;
                    item->overridden = true;
                }
                item = item->next;
            }

            if (!collided_expected_item) {
                printf("NOK\nunexpected key collision\n");
                return 1;
            }

            if (collided_expected_item->value != old_value) {
                printf("NOK\nunexpected memory address for value of collided key\n");
                return 1;
            }

            printf("OK\n");
        }
    }
    
    printf("all items inserted, %d entries (%d collisions)\n", expected_num_map_entries, num_items - expected_num_map_entries);

    // verify all items
    expected_item_t *expected_item = expected_items_head;
    while (expected_item) {
        if (!expected_item->overridden) {
            int *value = hashmap_get(map, expected_item->key);
            if (value != expected_item->value) {
                printf("unexpected memory address for value\n");
                return 1;
            }
        }

        expected_item = expected_item->next;
    }
    printf("all entries verified\n");

    // check distribution and list size
    printf("checking structural quality...\n");
    int num_hashes = 0;
    int max_size = 0;
    for (int i=0; i<HASH_COMBINATIONS; i++) {
        hashmap_item_t *map_item = map->items[i];
        if (!map_item) {
            continue;
        }
        
        num_hashes++;

        int size = 0;
        while (map_item) {
            size++;
            map_item = map_item->next;
        }

        if (size > max_size) {
            max_size = size;
        }
    }
    printf("%d hashes out of %d combinations used (%.1f %%), maximum list size %d\n", num_hashes, HASH_COMBINATIONS, (double) num_hashes / HASH_COMBINATIONS * 100.0, max_size);

    size_t histogram_size = sizeof(int[max_size+1]);
    int *histogram_by_size = malloc(histogram_size);
    memset(histogram_by_size, 0, histogram_size);
    
    for (int i=0; i<HASH_COMBINATIONS; i++) {
        hashmap_item_t *map_item = map->items[i];
        
        int size = 0;
        while (map_item) {
            size++;
            map_item = map_item->next;
        }

        histogram_by_size[size]++;
    }

    for (int size=0; size<=max_size; size++) {
        printf("  list size %3d encountered %d times\n", size, histogram_by_size[size]);
    }
    free(histogram_by_size);

    printf("destroying map\n");
    destroy_hashmap(map, destroy_value);

    if (num_destroy_value_calls != expected_num_map_entries) {
        printf("difference in number of value destructor calls, got called %d times, expected %d\n", num_destroy_value_calls, expected_num_map_entries);
        return 1;
    }

    printf("destroying test structures\n");
    destroy_expected_items(expected_items_head);
    
    printf("--- hashmap tests completed\n");
}
