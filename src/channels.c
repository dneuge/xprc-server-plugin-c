#include <stdlib.h>
#include <string.h>

#include "channels.h"
#include "utils.h"

#define BAD_CHANNEL_ID_SEGMENT 0xFF

// FIXME: mutex?
// FIXME: test

typedef uint8_t channel_id_segment_t;

static channel_id_segment_t char_to_channel_id_segment(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a';
    } else if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A' + 26;
    } else if (ch >= '0' && ch <= '9') {
        return ch - '0' + 52;
    } else {
        return BAD_CHANNEL_ID_SEGMENT;
    }
}

static char channel_id_segment_to_char(channel_id_segment_t segment) {
    if (segment < 26) {
        return 'a' + segment;
    } else if (segment < 52) {
        return 'A' + segment - 26;
    } else if (segment < 62) {
        return '0' + segment - 52;
    } else {
        return '_';
    }
}

channel_id_t string_to_channel_id(char* s) {
    if (!s) {
        return BAD_CHANNEL_ID;
    }

    channel_id_t id = 0;
    channel_id_segment_t segment = 0;
    
    for (int i=0; i<4; i++) {
        segment = char_to_channel_id_segment(s[i]);
        if (segment == BAD_CHANNEL_ID_SEGMENT) {
            return BAD_CHANNEL_ID;
        }
        
        id = (id << 6) | segment;
    }

    return id;
}

void channel_id_to_string(channel_id_t id, char* destination) {
    if (!destination) {
        return;
    }

    for (int i=3; i>=0; i--) {
        destination[i] = channel_id_segment_to_char(id & 0b00111111);
        id = id >> 6;
    }
}

static inline uint32_t channel_table_address(channel_id_t id) {
    return id >> 6;
}

static inline uint8_t channel_subtable_address(channel_id_t id) {
    return id & 0b00111111;
}

channels_table_t* create_channels_table() {
    return zmalloc(sizeof(channels_table_t));
}

static void destroy_channels_subtable(channels_subtable_t **subtable_ref, channel_destructor_f destructor, void *destructor_ref) {
    channels_subtable_t *subtable = *subtable_ref;
    
    for (int i=0; i<CHANNELS_SEGMENT; i++) {
        channel_t *channel = subtable->channels[i];
        if (channel) {
            destructor(channel, destructor_ref);

            // destructors call to pop_channel which already frees the subtable when it falls empty
            // we must check if the subtable still exist and stop otherwise
            if (!*subtable_ref) {
                return;
            }
        }
    }
    
    free(subtable);
}

void destroy_channels_table(channels_table_t *table, channel_destructor_f destructor, void *destructor_ref) {
    if (!table || !destructor) {
        return;
    }

    table->destruction_pending = true;
    
    for (int i=0; i<CHANNELS_NUM_SUBTABLES; i++) {
        channels_subtable_t **subtable_ref = &(table->subtables[i]);
        if (*subtable_ref) {
            destroy_channels_subtable(subtable_ref, destructor, destructor_ref);
            table->subtables[i] = NULL;
        }
    }

    free(table);
}

channel_t* get_channel(channels_table_t *table, channel_id_t id) {
    if (!table) {
        return NULL;
    }

    channels_subtable_t *subtable = table->subtables[channel_table_address(id)];
    if (!subtable) {
        return NULL;
    }

    return subtable->channels[channel_subtable_address(id)];
}

bool has_channel(channels_table_t *table, channel_id_t id) {
    return get_channel(table, id) != NULL;
}

channel_t* pop_channel(channels_table_t *table, channel_id_t id) {
    if (!table) {
        return NULL;
    }

    uint32_t table_address = channel_table_address(id);
    channels_subtable_t *subtable = table->subtables[table_address];
    if (!subtable) {
        return NULL;
    }
    
    uint8_t subtable_address = channel_subtable_address(id);
    channel_t *channel = subtable->channels[subtable_address]; // TODO: quit early if channel is NULL, subtable cleanup will not be needed
    subtable->channels[subtable_address] = NULL;

    // remove subtable if all channels are gone
    bool empty = true;
    for (int i=0; i<CHANNELS_SEGMENT; i++) {
        if (subtable->channels[i]) {
            empty = false;
            break;
        }
    }
    
    if (empty) {
        free(subtable);
        table->subtables[channel_table_address(id)] = NULL;
    }

    return channel;
}

bool put_channel(channels_table_t *table, channel_t *channel) {
    if (!table || !channel || table->destruction_pending) {
        return false;
    }
    
    uint32_t table_address = channel_table_address(channel->id);
    channels_subtable_t *subtable = table->subtables[table_address];
    if (!subtable) {
        // initialize new subtable
        subtable = zmalloc(sizeof(channels_subtable_t));
        if (!subtable) {
            return false;
        }

        table->subtables[table_address] = subtable;
    }
    
    uint8_t subtable_address = channel_subtable_address(channel->id);
    if (subtable->channels[subtable_address]) {
        // channel is already registered, do not replace
        return false;
    }
    
    subtable->channels[subtable_address] = channel;
    return true;
}

void request_channel_destruction(channels_table_t *table, channel_id_t id) {
    channel_t *channel = get_channel(table, id);
    if (!channel) {
        return;
    }

    channel->destruction_requested = true;
}
