#ifndef UTILS_H
#define UTILS_H

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

char* copy_string(char *s);
char* copy_partial_string(char *s, int length);
char* dynamic_sprintf(char *format, ...);
char* dynamic_vsprintf(char *format, va_list args);
bool ends_with(char *haystack, char *needle);
int num_digits(int value);
int64_t millis_of_timespec(struct timespec *ts);
int strpos(char *haystack, char *needle, int start);
void* zalloc(size_t size);

#endif
