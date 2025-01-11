#ifndef UTILS_H
#define UTILS_H

/**
 * @file utils.h miscellaneous helper functions
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/**
 * Compiler macro to determine maximum of two values.
 * @param x first value
 * @param y second value
 * @return x if greater than y, otherwise y
 */
#define MAX(x,y) (((x)>(y)) ? (x) : (y))

/// escape character that can be added in front of characters that would otherwise conflict with syntax
#define ESCAPE_CHAR '\\'

/**
 * Allocates memory and copies the contents from given source.
 * @param src source to copy from; must have sufficient size
 * @param size number of bytes to copy; must not exceed source size
 * @return pointer to copy; NULL on error
 */
void* copy_memory(void *src, size_t size);

/**
 * Copies the given string up to null-termination.
 * @param s null-terminated string to copy; may be NULL
 * @return copy of the string up to null-termination; NULL if input was NULL or on error
 */
char* copy_string(char *s);

/**
 * Copies the given string up to the specified length (ignoring null-termination) and terminates it with an additional
 * NULL character.
 * @param s string to copy; may be NULL
 * @param length exact number of characters to copy
 * @return null-terminated copy of the specified string segment
 */
char* copy_partial_string(char *s, size_t length);

/**
 * Copies the given string up to null-termination, removing each leading #ESCAPE_CHAR of a 2-byte sequence *without*
 * any transformation of possible escape sequences; see #copy_partial_unescaped_string() for examples.
 * @param s null-terminated string to copy without leading #ESCAPE_CHAR
 * @return a copy of the string with leading #ESCAPE_CHAR removed from 2-byte sequences; NULL if input was NULL or on error
 */
char* copy_unescaped_string(char *s);

/**
 * Copies the given string up to the specified length, removing each leading #ESCAPE_CHAR of a 2-byte sequence *without*
 * any transformation of possible escape sequences (for example `\n` would just become `n`, not a line-break).
 *
 * More examples:
 * - `"abc"` => `"abc"`
 * - `"ab\c"` => `"abc"`
 * - `"ab\\c"` => `"ab\c"`
 * - `"ab\\\c"` => `"ab\c"`
 * - `"a\b\\c"` => `"ab\c"`
 *
 * @param s string to copy without leading #ESCAPE_CHAR
 * @param max_length number of characters to process from input string
 * @return a copy of the string (up to max_length) with leading #ESCAPE_CHAR removed from 2-byte sequences, terminated by NULL; NULL if input was NULL or on error
 */
char* copy_partial_unescaped_string(char *s, int max_length);

/**
 * Formats a null-terminated string like a standard sprintf but allocates all required memory to store the string.
 * @param format formatting string; see vsnprintf for available syntax
 * @param ... parameters to be formatted
 * @return the formatted string; NULL on error
 */
char* dynamic_sprintf(char *format, ...);

/**
 * Formats a null-terminated string like a standard vsprintf but allocates all required memory to store the string.
 * @param format formatting string; see vsnprintf for available syntax
 * @param args parameters to be formatted, provided as a vararg list
 * @return the formatted string; NULL on error
 */
char* dynamic_vsprintf(char *format, va_list args);

/**
 * Tests if a string ends with the given needle.
 * @param haystack null-terminated string expected to end with needle
 * @param needle expectation as null-terminated string
 * @return true if haystack string ends with needle; false if not or any string is NULL
 */
bool ends_with(char *haystack, char *needle);

/**
 * Returns the number of digits of the given value, omitting the sign (`-42` will indicate `2` digits, not `3`).
 * @param value value to count the digits for
 * @return number of digits; sign will not be counted
 */
int num_digits(int value);

/**
 * Returns the number of milliseconds of the given timestamp (via floored nanosecond division).
 * @param ts timestamp to calculate milliseconds for
 * @return milliseconds of timestamp
 */
int64_t millis_of_timespec(struct timespec *ts);

/**
 * Calculates a timespec set to current time plus an offset.
 * @param out    reference to existing timespec to set; may get set to garbage if operation fails
 * @param base   base as required by timespec_get (e.g. TIME_UTC)
 * @param millis offset in milliseconds to add; only positive values (incl. zero) are supported
 * @return true if successful, false on error; out may hold garbage if operation fails
 */
bool timespec_now_plus_millis(struct timespec *out, int base, uint16_t millis);

/**
 * Returns the offset of the first occurrence of the given needle in a string.
 * @param haystack null-terminated string to search on
 * @param needle null-terminated string to search for
 * @param start offset for haystack to start searching from; negative offset starts search from the end of string
 * @return index of first occurrence of needle in haystack; negative if not found, any string is NULL or on error
 */
int strpos(char *haystack, char *needle, int start);

/**
 * Returns the offset of the first occurrence of the given needle in a string, ignoring all leading #ESCAPE_CHAR on the
 * haystack while searching but still counting them (for start index and result).
 * @param haystack null-terminated string to search on; leading #ESCAPE_CHAR on 2-byte sequences will be ignored
 * @param needle null-terminated string to search for; limited to only one character that is not the #ESCAPE_CHAR
 * @param start offset for haystack to start searching from; negative offset starts search from the end of string
 * @return index of first occurrence of needle in haystack; negative if not found, any string is NULL or on error; -2 for unsupported cases
 */
int strpos_unescaped(char *haystack, char *needle, int start);

/**
 * Counts the number of needle character occurrences in the given string up to the specified string length or a null
 * character, whatever comes first.
 * @param s string to count occurrences on
 * @param needle character to count occurrences of
 * @param length maximum number of characters to test from start of string
 * @return number of needle occurrences
 */
int count_chars(char *s, char needle, int length);

/**
 * Allocates memory of the specified size and zeroes it.
 * @param size number of bytes to allocate
 * @return pointer to start of zeroed memory; NULL if memory allocation failed
 */
void* zalloc(size_t size); // FIXME: migrate all calls to zmalloc, then delete

/**
 * Allocates memory of the specified size and zeroes it.
 * @param size number of bytes to allocate
 * @return pointer to start of zeroed memory; NULL if memory allocation failed
 */
void* zmalloc(size_t size);

/**
 * Attempts to parse the given string to an integer; destination will only be manipulated if successful.
 * @param dest where to write the parsed value to; will not be modified in case of error
 * @param s null-terminated string to parse
 * @return true if successfully parsed, false on error
 */
bool parse_int(int *dest, char *s);

/**
 * Sets the current thread's name according to the given format string. The name will be cut off at the
 * maximum possible length.
 * @param format formatting string; see vsnprintf for available syntax
 * @param ... parameters to be formatted
 */
void set_current_thread_name(char *format, ...);

#endif
