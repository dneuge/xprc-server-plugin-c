#ifndef ERRORS_H
#define ERRORS_H

/**
 * @file errors.h
 * Defines generic constants and the type definition #error_t for error codes used throughout the application.
 * Some modules define their own error codes which start at the offsets defined by this file.
 */

/**
 * Indicates that no error has occurred.
 */
#define ERROR_NONE 0

/**
 * Indicates that an error has occurred for which no specific error code has been assigned.
 */
#define ERROR_UNSPECIFIC 1

/**
 * Used when memory allocation has failed, for example during data structure or copy creation.
 */
#define ERROR_MEMORY_ALLOCATION 2

/**
 * Used when the requested operation cannot be performed or completed because a thread lock could not be acquired
 * where thread-safety is required.
 */
#define ERROR_MUTEX_FAILED 3

/**
 * Used when a data structure is pending destruction, including parent structures shutting down.
 * This error may get indicated by "sleeping" threads that just got woken up due to a dependency having been requested
 * to shut down. It is more for informational purpose in those cases as it documents an expected outcome of
 * asynchronous operations getting aborted.
 */
#define ERROR_DESTRUCTION_PENDING 4

/**
 * The requested operation was partially completed but some part failed.
 * This is used e.g. when loading a settings from a file where the format is valid but not all keys are understood
 * due to a change in the plugin version or similar - the result may be still be usable to some extent.
 */
#define ERROR_INCOMPLETE 5

/**
 * Start offset for errors related to the requests module, see: requests.h
 */
#define REQUEST_ERROR_BASE 10

/**
 * Start offset for errors related to the network module, see: network.h
 */
#define NETWORK_ERROR_BASE 20

/**
 * Start offset for errors related to the task scheduler module, see: task_schedule.h
 */
#define TASK_SCHEDULE_ERROR_BASE 30

/**
 * Start offset for errors related to the session module, see: session.h
 */
#define SESSION_ERROR_BASE 40

/**
 * Start offset for errors related to the data proxy module, see: dataproxy.h
 */
#define DATAPROXY_ERROR_BASE 50

/**
 * Start offset for errors related to the settings manager module, see: settings_manager.h
 */
#define SETTINGS_MANAGER_ERROR_BASE 60

/**
 * Indicates if and which error has occurred, #ERROR_NONE indicates success.
 */
typedef int error_t;

#endif
