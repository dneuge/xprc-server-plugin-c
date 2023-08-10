#ifndef SESSION_H
#define SESSION_H

/**
 * @file session.h client session handling
 *
 * Defines the data structure used for client session tracking and provides high-level functions to submit messages
 * to sessions.
 *
 * New sessions start in a handshake mode where client version and authentication need to be exchanged and verified
 * (performed by the server, see server.h). Once the session has entered #SESSION_PHASE_LOGGED_IN it is ready to
 * receive requests which spawn command channels.
 *
 * The high-level functions provided by this module are then used to communicate information back to the client:
 *
 * - Messages always belong to a channel; the server can only (but asynchronously) send messages in reply to the client.
 * - Channel messages sent to the client always indicate whether the channel stays open (`+`) or closes (`-`) after that
 *   message. Once a channel was closed the server can send no further messages on it. The high-level functions take
 *   care of that and use #SESSION_ERROR_INVALID_CHANNEL_STATE to indicate if the channel's life-cycle was incompatible
 *   with the intended message type.
 * - An error sent through #error_channel() always closes a channel as any error means that something went wrong with
 *   the command so it should not continue and instead terminate, so there is no point in keeping the channel open.
 *   Attempting to send further messages will fail, there also is no legal way of reopening the channel. As only the
 *   first error message will be seen by the client this can ease error reporting as the most specific message usually
 *   appears first; any successive, less specific error messages will be kept from the client.
 * - The first message on a channel needs to explicitly confirm successful command creation through #confirm_channel()
 *   or indicate failure via #error_channel(). Many commands also send a special first message but depending on command
 *   specification the first message may also already contain detail data.
 * - Subsequent messages sent to a confirmed channel need to use #continue_channel().
 * - The last message before command termination should be sent via #finish_channel() to close the channel afterwards.
 *   - If no message is provided, the channel will indicate termination through `-ACK` as per protocol specification.
 *   - This can also be used for the initial (single!) message of instantaneously terminating commands.
 */

#include <stdint.h>
#include <time.h>

typedef struct _session_t session_t;

#include "channels.h"
#include "network.h"
#include "server.h"

/// indicates that the channel does not exist
#define SESSION_ERROR_NO_SUCH_CHANNEL       (SESSION_ERROR_BASE + 0)
/// indicates that the channel does not have a suitable state for the requested operation
#define SESSION_ERROR_INVALID_CHANNEL_STATE (SESSION_ERROR_BASE + 1)

/// session is in handshake, awaiting version to be indicated by client
#define SESSION_PHASE_AWAIT_VERSION 0
/// session is in handshake, awaiting password to be submitted by client
#define SESSION_PHASE_AWAIT_PASSWORD 1
/// session has failed handshake or authentication
#define SESSION_PHASE_FAILED 2
/// session is authorized and valid
#define SESSION_PHASE_LOGGED_IN 3

/// use the current timestamp when the message is actually being set out
#define CURRENT_TIME_REFERENCE -1

/// the phase a session is currently in; see SESSION_PHASE_*
typedef uint8_t session_phase_t;

/// a single XPRC client session
typedef struct _session_t {
    /// the timestamp to reference all messages to in native representation; set at start of session
    struct timespec reference_time;
    /// the timestamp to reference all messages to in milliseconds; set at start of session
    int64_t reference_millis;
    /// the current phase/state of the session; handled by the server, see server.h
    session_phase_t phase;
    /// the client's reported protocol version
    int client_version;
    /// all channels of this session
    channels_table_t *channels;
    /// the network connection of this session
    network_connection_t *connection;
    /// the XPRC server instance this session belongs to
    server_t *server;
    /// protects concurrent session access
    mtx_t mutex;
    /// if the session is pending destruction (true) all actions must come to an end, locking is no longer permitted
    bool destruction_pending;
} session_t;

/**
 * Creates a new session.
 * @param session will be set to the created session instance
 * @param connection the network connection the session is being opened on
 * @param server the server handling the session
 * @return error code; #ERROR_NONE on success
 */
error_t create_session(session_t **session, network_connection_t *connection, server_t *server);
/**
 * Destroys the session incl. the channels table.
 * @param session session to be destroyed
 */
void destroy_session(session_t *session);

/**
 * Attempts to lock the session for thread-safety.
 * @param session session to lock
 * @return true if lock has been gained, false if locking failed (session may be pending for destruction)
 */
bool lock_session(session_t *session);
/**
 * Unlocks a previously locked session; must only be called if a lock is currently held.
 * @param session session to unlock
 */
void unlock_session(session_t *session);

/**
 * Returns the current system time in milliseconds relative to session reference; useful to refer to events/data
 * collection if actual messages are only sent out at a later time.
 * @param session session to reference
 * @return milliseconds since session reference; negative on error
 */
int64_t millis_since_reference(session_t *session);

/**
 * Sends a message confirming the channel and keeps it open for further messages (`+ACK CHAN`).
 * @param session session to send the message for
 * @param channel_id channel to send the message for
 * @param millis_since_reference session-relative timestamp to indicate on the message; use #millis_since_reference() or #CURRENT_TIME_REFERENCE
 * @param message the message to send as part of the acknowledgement; can be NULL to only confirm without payload
 * @return error code; #ERROR_NONE on success
 */
error_t confirm_channel(session_t *session, channel_id_t channel_id, int64_t millis_since_reference, char *message);

/**
 * Sends a message continuing on a previously confirmed channel and keeps it open (`+CHAN`).
 * @param session session to send the message for
 * @param channel_id channel to send the message for
 * @param millis_since_reference session-relative timestamp to indicate on the message; use #millis_since_reference() or #CURRENT_TIME_REFERENCE
 * @param message the message to send; can be NULL to only trigger the channel without payload
 * @return error code; #ERROR_NONE on success
 */
error_t continue_channel(session_t *session, channel_id_t channel_id, int64_t millis_since_reference, char *message);

/**
 * Sends a message and closes a channel but does not indicate an error (`-CHAN` or `-ACK CHAN`).
 * @param session session to send the message for
 * @param channel_id channel to send the message for
 * @param millis_since_reference session-relative timestamp to indicate on the message; use #millis_since_reference() or #CURRENT_TIME_REFERENCE
 * @param message the message to send; can be NULL to only close the channel error-free without a payload
 * @return error code; #ERROR_NONE on success
 */
error_t finish_channel(session_t *session, channel_id_t channel_id, int64_t millis_since_reference, char *message);

/**
 * Closes the channel, reporting an error message (`-ERR CHAN`).
 * @param session session to send the message for
 * @param channel_id channel to send the message for
 * @param millis_since_reference session-relative timestamp to indicate on the message; use #millis_since_reference() or #CURRENT_TIME_REFERENCE
 * @param message the message to send; can be NULL to omit an error message
 * @return error code; #ERROR_NONE on success
 */
error_t error_channel(session_t *session, channel_id_t channel_id, int64_t millis_since_reference, char *message);

#endif
