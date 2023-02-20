#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"

char* dynamic_sprintf(char *format, ...) {
    va_list args;
    va_start(args, format);
    char *out = dynamic_vsprintf(format, args);
    va_end(args);

    return out;
}

char* dynamic_vsprintf(char *format, va_list args) {
    int required_size = vsnprintf(NULL, 0, format, args);
    if (required_size < 1) {
        return NULL;
    }

    int out_size = required_size + 1;
    char *out = zalloc(out_size);
    if (out) {
        int res = vsnprintf(out, out_size, format, args);
        if (res != required_size) {
            free(out);
            out = NULL;
        }
    }

    return out;
}

bool ends_with(char *haystack, char *needle) {
    if (!haystack || !needle) {
        return false;
    }

    int haystack_len = strlen(haystack);
    int needle_len = strlen(needle);
    if (haystack_len < needle_len) {
        return false;
    }

    return strncmp(&haystack[haystack_len-needle_len-1], needle, needle_len);
}

int strpos(char *haystack, char *needle, int start) {
    if (!haystack || !needle) {
        return -1;
    }

    int haystack_len = strlen(haystack);
    int needle_len = strlen(needle);

    if (haystack_len < needle_len) {
        return -1;
    }

    // negative start offset = from back
    if (start < 0) {
        start += haystack_len;
    }

    if ((start < 0) || (start >= haystack_len)) {
        return -1;
    }

    int max_offset = haystack_len - needle_len;
    for (int i=start; i<max_offset; i++) {
        if (!strncmp(&haystack[i], needle, needle_len)) {
            return i;
        }
    }

    return -1;
}

void* zalloc(size_t size) {
    void *addr = malloc(size);
    if (addr) {
        memset(addr, 0, size);
    }
    return addr;
}

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
