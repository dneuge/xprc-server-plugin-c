#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdbool.h>
#include <stdint.h>

#define HASH_BYTES 2
#define HASH_COMBINATIONS (1 << (HASH_BYTES * 8))

#if HASH_BYTES <= 1
#define HASH_TYPE uint8_t
#elif HASH_BYTES <= 2
#define HASH_TYPE uint16_t
#elif HASH_BYTES <= 4
#define HASH_TYPE uint32_t
#else
#error unsupported hash type
#endif

typedef void hashmap_value_destructor_f (char *key, void *value);

typedef struct _hashmap_item_t hashmap_item_t;
typedef struct _hashmap_item_t {
    char *key;
    void *value;
    hashmap_item_t *next;
} hashmap_item_t;

typedef struct {
    hashmap_item_t *items[HASH_COMBINATIONS];
} hashmap_t;

hashmap_t* create_hashmap();
void destroy_hashmap(hashmap_t *map, hashmap_value_destructor_f value_destructor);

void* hashmap_get(hashmap_t *map, char *key);
bool hashmap_put(hashmap_t *map, char *key, void *value, void **old_value);

#endif
