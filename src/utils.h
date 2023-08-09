#ifndef UTILS_H
#define UTILS_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/// escape character that can be added in front of characters that would otherwise conflict with syntax
#define ESCAPE_CHAR '\\'

char* copy_string(char *s);
char* copy_partial_string(char *s, int length);
char* copy_unescaped_string(char *s); // removes escape characters from the copy
char* copy_partial_unescaped_string(char *s, int max_length); // removes escape characters from the copy
char* dynamic_sprintf(char *format, ...);
char* dynamic_vsprintf(char *format, va_list args);
bool ends_with(char *haystack, char *needle);
int num_digits(int value);
int64_t millis_of_timespec(struct timespec *ts);
int strpos(char *haystack, char *needle, int start);
int strpos_unescaped(char *haystack, char *needle, int start); // searches for a single-byte needle that has not been escaped; returns -2 in unsupported cases
int count_chars(char *s, char needle, int length);
void* zalloc(size_t size);

#endif
