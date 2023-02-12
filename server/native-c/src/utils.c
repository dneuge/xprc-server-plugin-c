#include <stdlib.h>
#include <string.h>

#include "utils.h"

char* copy_partial_string(char *s, int length) {
    char *copy = malloc(length + 1);
    if (!copy) {
        return NULL;
    }
    
    memcpy(copy, s, length);
    copy[length] = 0;
    
    return copy;
}

char* copy_string(char *s) {
    if (!s) {
        return NULL;
    }

    return copy_partial_string(s, strlen(s));
}

int num_digits(int value) {
    int digits = 0;

    if (value == 0) {
        return 1;
    }
  
    if (value < 0) {
        value = -value;
    }

    while (value > 0) {
        value /= 10;
        digits++;
    }

    return digits;
}

int64_t millis_of_timespec(struct timespec *ts) {
    if (ts->tv_sec < 0) {
        return -1;
    }
    
    return ((int64_t) ts->tv_sec * 1000) + (ts->tv_nsec / 1000000);
}
