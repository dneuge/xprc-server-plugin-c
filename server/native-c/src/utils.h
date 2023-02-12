#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <time.h>

char* copy_string(char *s);
char* copy_partial_string(char *s, int length);
int num_digits(int value);
int64_t millis_of_timespec(struct timespec *ts);

#endif
