#ifndef CHANNELS_H
#define CHANNELS_H

#include <stdbool.h>
#include <stdint.h>

/* Channel IDs are specified as 4 alpha-numeric characters [a-zA-Z0-9] which makes
 * 62 possible values per character (14.7M combinations).
 * Instead of hashing that channel ID as a char[] we represent it as a 24 bit unsigned integer
 * (62~64 = 6 bits * 4 = 24 bits).
 * Instead of a hashmap we use a 62^3 large array of pointers (~1.9MB) to "sub-tables"
 * and, when needed, create those as fixed arrays of the last address segment (62 positions,
 * lowest 6 bit of the channel ID). This is basically a "sparse array" over the full 14.7M
 * entries which requires far less memory than the >118MB that would be needed for a full-size
 * array (64 bit = 8 bytes per address) while still being able to perform fast lookups.
 */

typedef uint32_t channel_id_t;

#define CHANNELS_SEGMENT 62
#define CHANNELS_NUM_SUBTABLES (CHANNELS_SEGMENT * CHANNELS_SEGMENT * CHANNELS_SEGMENT)

#define BAD_CHANNEL_ID 0xFFFFFFFF

typedef struct {
    channel_id_t id;
    // FIXME: add on_destruction callback
} channel_t;

typedef struct {
    channel_t *channels[CHANNELS_SEGMENT];
} channels_subtable_t;

typedef struct {
    channels_subtable_t *subtables[CHANNELS_NUM_SUBTABLES];
} channels_table_t;

channel_id_t string_to_channel_id(char* s);
void channel_id_to_string(channel_id_t id, char* destination);

channels_table_t* create_channels_table();
void destroy_channels_table(channels_table_t *table);

channel_t* get_channel(channels_table_t *table, channel_id_t id);
bool has_channel(channels_table_t *table, channel_id_t id);
channel_t* pop_channel(channels_table_t *table, channel_id_t id);
bool put_channel(channels_table_t *table, channel_t *channel);

#endif
