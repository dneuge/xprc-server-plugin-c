#include <stdlib.h>
#include <string.h>

#include "utils.h"

#include "hashmap.h"

#if HASH_BYTES <= 1
#define HASH_TYPE uint8_t
#elif HASH_BYTES <= 2
#define HASH_TYPE uint16_t
#elif HASH_BYTES <= 4
#define HASH_TYPE uint32_t
#else
#error unsupported hash type
#endif

static inline uint8_t ror8(uint8_t value, int num);
#if defined(__GNUC__) && (defined(__x86_64__) || defined(_X86_))
#include <x86intrin.h>
static inline uint8_t ror8(uint8_t value, int num) {
    return __rorb(value, num);
}
#else
#warning "using unoptimized ror8 operation"
static inline uint8_t ror8(uint8_t value, int num) {
    // FIXME: verify
    return ((value >> (num%8)) & 0xFF) | ((value << (8-num%8)) & 0xFF);
}
#endif

static HASH_TYPE compute_hash(char *key) {
    int key_length = strlen(key);
    uint8_t hash[HASH_BYTES] = {0};

    for (int k=0; k<key_length; k++) {
        int h = k % HASH_BYTES;
        uint8_t key_byte = key[k];
        uint8_t hash_byte = ror8(hash[h], 1); // roll hash for better distribution, especially with 7-bit keys

        hash_byte = key_byte ^ hash_byte;

        hash[h] = hash_byte;
    }

    HASH_TYPE out = 0;
    for (int i=0; i<HASH_BYTES; i++) {
        out = (out << 8) + hash[i];
    }

    return out;
}

hashmap_t* create_hashmap() {
    return zmalloc(sizeof(hashmap_t));
}

static void destroy_item(hashmap_item_t *item, hashmap_value_destructor_f value_destructor) {
    while (item) {
        hashmap_item_t *next = item->next;
        
        if (value_destructor) {
            value_destructor(item->key, item->value);
        }
    
        free(item->key);
        free(item);

        item = next;
    }
}

void destroy_hashmap(hashmap_t *map, hashmap_value_destructor_f value_destructor) {
    if (!map) {
        return;
    }

    for (int i=0; i<HASH_COMBINATIONS; i++) {
        destroy_item(map->items[i], value_destructor);
    }
    
    free(map);
}

void* hashmap_get(hashmap_t *map, char *key) {
    if (!map || !key) {
        return NULL;
    }

    HASH_TYPE hash = compute_hash(key);
    hashmap_item_t *item = map->items[hash];
    while (item) {
        if (!strcmp(item->key, key)) {
            return item->value;
        }
        item = item->next;
    }
    return NULL;
}

bool hashmap_put(hashmap_t *map, char *key, void *value, void **old_value) {
    if (!map || !key || !old_value) {
        return false;
    }

    HASH_TYPE hash = compute_hash(key);
    hashmap_item_t **reference = &(map->items[hash]);
    while (*reference) {
        hashmap_item_t *item = *reference;
        if (!strcmp(item->key, key)) {
            // existing item value is being replaced
            *old_value = item->value;
            item->value = value;
            return true;
        }
        reference = &(item->next);
    }

    // if we get here we need to add a new item
    hashmap_item_t *item = zmalloc(sizeof(hashmap_item_t));
    if (!item) {
        return false;
    }

    item->key = copy_string(key);
    if (!item->key) {
        free(item);
        return false;
    }
    
    item->value = value;

    *reference = item;

    return true;
}

bool is_hashmap_empty(hashmap_t *map) {
    if (!map) {
        return true;
    }

    for (int i=0; i<HASH_COMBINATIONS; i++) {
        if (map->items[i]) {
            return false;
        }
    }

    return true;
}

list_t* hashmap_reference_keys(hashmap_t *map) {
    if (!map) {
        return NULL;
    }

    list_t *out = create_list();
    if (!out) {
        return NULL;
    }

    bool success = true;
    for (int i=0; i<HASH_COMBINATIONS; i++) {
        hashmap_item_t *map_item = map->items[i];
        while (map_item) {
            if (!map_item->key || !list_append(out, map_item->key)) {
                success = false;
                break;
            }

            map_item = map_item->next;
        }
    }

    if (!success) {
        destroy_list(out, NULL);
        out = NULL;
    }

    return out;
}

list_t* hashmap_copy_keys(hashmap_t *map) {
    if (!map) {
        return NULL;
    }

    list_t *out = hashmap_reference_keys(map);
    if (!out) {
        return NULL;
    }

    bool success = true;
    list_item_t *item = out->head;
    while (item) {
        item->value = copy_string(item->value);
        if (!item->value) {
            success = false;

            // we must only free already copied strings, i.e. everything that preceeds (prev)
            // everything that follows (next) has not been copied yet
            item = item->prev;
            while (item) {
                free(item->value);
                item->value = NULL;

                item = item->prev;
            }

            break;
        }

        item = item->next;
    }

    if (!success) {
        // copy failed - remaining values in list are NOT copies but shared pointers and must not be freed
        destroy_list(out, NULL);
        out = NULL;
    }

    return out;
}
