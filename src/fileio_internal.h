#ifndef XPRC_FILEIO_INTERNAL_H
#define XPRC_FILEIO_INTERNAL_H

typedef struct file_handle_s file_handle_t;

typedef uint8_t file_mode_t;
#define FILE_MODE_READ  (1<<0)
#define FILE_MODE_WRITE (1<<1)

/**
 * Initializes/resets the given file handle.
 * @param fh file handle
 * @return error code; #ERROR_NONE on success
 */
static error_t init_file_handle(file_handle_t *fh);

/**
 * Opens the specified file in the requested mode.
 *
 * @param fh file handle (will be modified, must have been initialized and either be unused or closed)
 * @param mode OR'ed combination of flags, see FILE_MODE defines
 * @param path UTF-8 encoded file path to open
 * @return error code; #ERROR_NONE on success
 */
static error_t open_file(file_handle_t *fh, file_mode_t mode, char *path);

/**
 * Reads as many bytes as possible from the given file.
 *
 * @param num_read will be set to the number of bytes read into the given buffer
 * @param fh file handle
 * @param buffer buffer to write to
 * @param buffer_size size of buffer, maximum number of bytes to read
 * @return error code; #ERROR_NONE on success
 */
static error_t read_bytes(size_t *num_read, file_handle_t *fh, char *buffer, size_t buffer_size);

/**
 * Attempts to write the specified number of bytes to the given file.
 *
 * Both num_written and the returned error code need to be checked to confirm that all intended data has been written.
 * Data may not get flushed before the handle is being closed.
 *
 * @param num_written will be set to the number of bytes which have been written (also check error code)
 * @param fh file handle
 * @param bytes bytes to be written
 * @param size number of bytes to try writing
 * @return error code; #ERROR_NONE on success
 */
static error_t write_bytes(size_t *num_written, file_handle_t *fh, char *bytes, size_t size);

/**
 * Closes the specified file.
 *
 * May return #ERROR_INCOMPLETE in case file failed to close on platform layer but should be regarded
 * as having been closed.
 *
 * @param fh file handle
 * @return error code; #ERROR_NONE on success
 */
static error_t close_file(file_handle_t *fh);

/**
 * Checks if the given file handle is currently open.
 *
 * @param fh file handle
 * @return true if open, false if closed or on error
 */
static bool is_open_file(file_handle_t *fh);

/**
 * Checks if the file handle's cursor reached the end of file (EOF).
 *
 * @param fh file handle
 * @return true if cursor reached EOF, file has been closed or is inaccessible; false if cursor is not at end
 */
static bool check_eof(file_handle_t *fh);

#endif //XPRC_FILEIO_INTERNAL_H