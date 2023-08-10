#ifndef CHANNELS_H
#define CHANNELS_H

/**
 * @file channels.h XPRC protocol-level channel handling used for sessions and commands
 *
 * Channels are an essential part of sessions and managed as part of those: Every command run by clients needs to be
 * associated with a channel so that the server and/or command is able to asynchronously communicate results back.
 * A channel is identified by a 4-character alpha-numeric ID on protocol-level. All IDs are assigned from client to
 * server; the server only denies concurrent use of already/still (not freed) channels. If a channel stays open for
 * more than one server reply, it will be acknowledged towards the client and marked as #CHANNEL_STATE_CONFIRMED.
 *
 * Each session has its own set of channels, tracked in a #channels_table_t. The protocol specifies channel IDs as 4
 * alpha-numeric characters `[a-zA-Z0-9]` which make 62 possible values per character (14.7M combinations).
 * For easier handling that channel ID is encoded as a 24 bit unsigned integer for internal processing
 * (62~64 = 6 bits * 4 = 24 bits).
 *
 * #channels_table_t indexes all channels in two stages: The first stage uses a 62^3 large array of pointers (~1.9MB)
 * for the first 3 characters that is used to reference sub-tables which are only created if at least one channel
 * exists in them. As the second stage, those sub-tables handle the last address segment (62 positions,
 * lowest 6 bits of the channel ID). This is basically a "sparse array" over the full range of all 14.7M
 * possible channels which requires far less memory than the >118MB that would be needed for a full-size
 * array (64 bits = 8 bytes per address) while still being able to perform fast lookups.
 *
 * Thread-safety has to be ensured by callers for all data access.
 *
 * Note that channel handling, sessions and server processes are intertwined; for the complete picture also read:
 *
 * - @ref server.h
 * - @ref session.h
 */

#include <stdbool.h>
#include <stdint.h>

/// compact 24-bit encoding of an XPRC channel ID
typedef uint32_t channel_id_t;

/// number of combinations (allowed characters) for one segment (character) of an XPRC channel ID
#define CHANNELS_SEGMENT 62
/// number of sub-tables allocated for immediate access to the first 3 segments of an XPRC channel ID
#define CHANNELS_NUM_SUBTABLES (CHANNELS_SEGMENT * CHANNELS_SEGMENT * CHANNELS_SEGMENT)

/// placeholder for a channel_id_t indicating an invalid channel
#define BAD_CHANNEL_ID 0xFFFFFFFF

typedef struct _channels_table_t channels_table_t;
/// describes the current state a channel is in; see CHANNEL_STATE_* defines
typedef uint8_t channel_state_t;

/// the channel has just been created, no confirmation has been sent yet
#define CHANNEL_STATE_INITIAL 0
/// channel confirmation has already been sent
#define CHANNEL_STATE_CONFIRMED 1
/// the channel has been closed and should no longer be referred to
#define CHANNEL_STATE_CLOSED 2

typedef struct _channel_t channel_t;

#include "command.h"

/**
 * single channel registration, tied to the assigned command
 */
typedef struct _channel_t {
    /// the channel's ID as requested by the client
    channel_id_t id;
    /// the current state of the channel; see CHANNEL_STATE_*
    channel_state_t state;
    /// channel destruction is requested by commands but carried out by server maintenance asynchronously
    bool destruction_requested;
    /// description of the command running on this channel; see command.h
    command_t *command;
    /// channel-specific reference data for the command; see command.h
    void *command_ref;
} channel_t;

/**
 * table for the last segment (character) of a channel ID
 */
typedef struct {
    /// all channels of the same first 3 segments indexed by their last (4th) segment
    channel_t *channels[CHANNELS_SEGMENT];
} channels_subtable_t;

/**
 * channel registration table for a session
 */
typedef struct _channels_table_t {
    /// only cleanup is allowed if destruction is pending (true)
    bool destruction_pending;
    /**
     * Links to final segment sub-table indexed by the first 3 segments (characters) of a channel ID. May point to NULL
     * as sub-tables are only allocated while they hold at least one channel.
     */
    channels_subtable_t *subtables[CHANNELS_NUM_SUBTABLES];
} channels_table_t;

/**
 * Destroys a channel; used to perform related operations in higher context.
 * @param channel channel to be destroyed; must be freed
 * @param ref context reference for the destructor
 */
typedef void (*channel_destructor_f)(channel_t *channel, void *ref);

/**
 * Parses a human-readable string to a compact encoded channel ID.
 * @param s (potential) channel ID as a string; content after the first 4 characters is ignored
 * @return compact encoded channel ID; #BAD_CHANNEL_ID if invalid
 */
channel_id_t string_to_channel_id(char* s);
/**
 * Formats a compact encoded channel ID to a 4-character human-readable string.
 * @param id channel ID to format
 * @param destination memory to output characters to; must have at least 4 characters allocated
 */
void channel_id_to_string(channel_id_t id, char* destination);

/**
 * Allocates and initializes a new channels table as used for a session.
 * @return channels table; NULL on error
 */
channels_table_t* create_channels_table();
/**
 * Destroys a channels table; the given destructor will be used for individual channels.
 * @param table channels table to be destroyed
 * @param destructor called to destroy individual channels
 * @param destructor_ref context reference for the channel destructor
 */
void destroy_channels_table(channels_table_t *table, channel_destructor_f destructor, void *destructor_ref);

/**
 * Looks up an existing channel by its ID.
 * @param table channels table to retrieve the channel from
 * @param id channel ID to look up; must be valid (otherwise wrong or illegal access will occur)
 * @return the requested channel; NULL if no such channel has been registered yet
 */
channel_t* get_channel(channels_table_t *table, channel_id_t id);
/**
 * Checks if the specified channel exists.
 * @param table channels table to check existence on
 * @param id channel ID to look up; must be valid (otherwise wrong or illegal access will occur)
 * @return true if the channel exists; false if not
 */
bool has_channel(channels_table_t *table, channel_id_t id);
/**
 * Removes and returns the specified channel if it exists.
 *
 * Calling this function also checks the #channels_subtable_t for remaining channels. The subtable will be destroyed
 * when the last channel has been removed.
 * @param table channels table to remove channel from
 * @param id channel ID to remove; must be valid (otherwise wrong or illegal access will occur)
 * @return the removed channel; NULL if no such channel has been registered
 */
channel_t* pop_channel(channels_table_t *table, channel_id_t id);
/**
 * Registers the given channel to the channels table; fails if a channel using the same ID has already been registered.
 * @param table channels table to register channel to
 * @param channel channel to be registered; must have a valid ID (otherwise wrong or illegal access will occur)
 * @return true if stored; false on error
 */
bool put_channel(channels_table_t *table, channel_t *channel);

/**
 * Marks the requested channel as to be destroyed.
 *
 * Meant to only be called as the last instruction of command termination as channel destruction will also destroy the
 * command calling this function; see command.h.
 *
 * Actual destruction will be performed asynchronously.
 * @param table channels table containing the channel to be destroyed
 * @param id ID of channel to be destroyed; may not exist but must be valid (otherwise wrong or illegal access will occur)
 */
void request_channel_destruction(channels_table_t *table, channel_id_t id);

#endif
