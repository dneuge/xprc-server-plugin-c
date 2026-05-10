#ifndef XPRC_FILEIO_H
#define XPRC_FILEIO_H

/**
 * @file fileio.h general handling of file input and output
 */

#include "errors.h"
#include "lists.h"
#include "utils.h"

/// Approximate maximum length for a file path on Linux; depends on actual kernel but 4k appears to be current standard.
#define LINUX_PATH_MAX_LENGTH 4096

/**
 * Estimated maximum length for a file path on Windows. Documentation specifies 32k plus whatever is needed to
 * expand the UNS prefix, *blindly* estimated here to 512 + 5 characters extra. Some documentation just mentions 32767
 * *wide* characters, however. Ideally, we should reserve too much memory when importing paths and stay under the lowest
 * limit when exporting them.
 *
 * Note that this may *only* apply to access conducted directly through the file system, requiring a \\?\ prefix
 * and possibly being incompatible with some APIs, otherwise users are required to change registry settings to get past
 * the legacy 260 character limit. Some documentation also mentions that we need to use Unicode/wide-character variants
 * to be able to use that.
 *
 * Source: Documentation licensed under CC-BY 4.0, Copyright Microsoft Corporation, see:
 * https://github.com/MicrosoftDocs/win32/blob/docs/desktop-src/FileIO/maximum-file-path-limitation.md
 * https://github.com/MicrosoftDocs/win32/blob/ff14af8b326deef58ec2500cd3fee5d6dcd1a3dd/desktop-src/FileIO/naming-a-file.md
 * https://github.com/MicrosoftDocs/sdk-api/blob/07512580a99bac226f8730c8f85344270f1beeff/sdk-api-src/content/fileapi/nf-fileapi-createfilew.md
 */
#define WINDOWS_PATH_MAX_LENGTH (32767 + 512 + 5)

#define PATH_MAX_LENGTH (MAX(WINDOWS_PATH_MAX_LENGTH, LINUX_PATH_MAX_LENGTH))

/// separator between directories and files (depends on compilation target)
#ifdef TARGET_WINDOWS
#define DIRECTORY_SEPARATOR '\\'
#else
#define DIRECTORY_SEPARATOR '/'
#endif

/// default line end sequence of the operating system (depends on compilation target)
#ifdef TARGET_WINDOWS
#define LINE_END_SEQUENCE "\r\n"
#define LINE_END_SEQUENCE_LENGTH (2)
#else
#define LINE_END_SEQUENCE "\n"
#define LINE_END_SEQUENCE_LENGTH (1)
#endif

/**
 * Splits the given string on all line end sequences.
 * @param lines will be set to the resulting list of lines as null-terminated strings without line end sequences
 * @param s string to split
 * @param length length of the string
 * @return error code; #ERROR_NONE on success
 */
error_t split_lines(list_t **lines, char *s, size_t length);

/**
 * Joins the given lines to a null-terminated string using the operating system's default line end sequence
 * (CR LF on Windows, otherwise LF).
 *
 * The last line will remain open; an empty line must be provided to end the string with a line-break.
 *
 * @param lines lines to be written to be joined
 * @return null-terminated string of all lines joined with line separators; NULL on error
 */
char* join_lines(list_t *lines);

/**
 * Reads the given file into memory.
 * @param data will be set to the read data plus an extra null termination
 * @param length will be set to the number of bytes read (length of data without extra null termination)
 * @param path path to file as null-terminated string
 * @return error code; #ERROR_NONE on success
 */
error_t read_file(char **data, size_t *length, char *path);

/**
 * Writes the given data into the specified file which will be overwritten if it already exists.
 * @param data data to be saved to the file
 * @param length number of bytes to write
 * @param path path to file as null-terminated string
 * @return error code; #ERROR_NONE on success
 */
error_t write_file(char *data, size_t length, char *path);

/**
 * Reads all lines from the given file.
 * @param lines will be set to the resulting list of lines without line end sequences
 * @param path path to file as null-terminated string
 * @return error code; #ERROR_NONE on success
 */
error_t read_lines_from_file(list_t **lines, char *path);

/**
 * Writes all lines into the given file which will be overwritten if it already exists.
 *
 * The operating system's default line end sequence will be used (CR LF on Windows, otherwise LF).
 *
 * The last line will not get terminated; an empty line must be provided to end the file with a line-break.
 *
 * @param lines lines to be written to the file
 * @param path path to file as null-terminated string
 * @return error code; #ERROR_NONE on success
 */
error_t write_lines_to_file(list_t *lines, char *path);

/**
 * Checks if the given file or directory exists.
 * @param path path to check for existence
 * @return true if the file/directory exists, false if not
 */
bool check_file_exists(char *path);

/**
 * Ensures that the given directory exists. The directory will be created if it does not exist yet.
 *
 * @param path directory path
 * @return error code; #ERROR_NONE on success
 */
error_t ensure_directory_exists(char *path);

#endif //XPRC_FILEIO_H
