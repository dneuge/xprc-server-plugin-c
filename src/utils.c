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
    va_list args_copy;
    va_copy(args_copy, args);
    int required_size = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);
    
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

    return !strncmp(&haystack[haystack_len-needle_len], needle, needle_len);
}

static bool is_escaped(char *s, int pos) {
    bool is_escaped = false;

    if (pos < 1) {
        return false;
    }

    // stop one character before the queried position
    for (int i=0; i<pos; i++) {
        if (is_escaped) {
            // this character was escaped, the next one will not be
            is_escaped = false;
        } else if (s[i] == ESCAPE_CHAR) {
            // this character was not escaped but it is the escape character, the next char will be escaped
            is_escaped = true;
        }
    }

    return is_escaped;
}

static int _strpos(char *haystack, char *needle, int start, bool skip_escapes) {
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
    for (int i=start; i<=max_offset; i++) {
        if (!strncmp(&haystack[i], needle, needle_len)) {
            if (skip_escapes && is_escaped(haystack, i)) {
                continue;
            }
            return i;
        }
    }

    return -1;
}

int strpos(char *haystack, char *needle, int start) {
    return _strpos(haystack, needle, start, false);
}

int strpos_unescaped(char *haystack, char *needle, int start) {
    // NOTE: search on potentially escaped strings is currently only supported for
    //       - a single character needle...
    //       - ... which is *NOT* the escape character
    //       unsupported cases *MUST NOT* be called, even though they are
    //       blocked by returning -1 which may lead to misinterpretation
    if ((strlen(needle) != 1) || (needle[0] == ESCAPE_CHAR)) {
        return -2;
    }
    
    return _strpos(haystack, needle, start, true);
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

char* copy_partial_unescaped_string(char *s, int max_length) {
    char *copy = zalloc(max_length + 1);
    if (!copy) {
        return NULL;
    }

    bool previous_was_escaped = false;
    int pos = 0;
    for (int i=0; i<max_length; i++) {
        char ch = s[i];

        if (previous_was_escaped) {
            // this character should be copied i.e. be "unescaped"
            previous_was_escaped = false;
        } else if (ch == ESCAPE_CHAR) {
            // this is an unescaped escape character, i.e. it starts an escape... don't copy it
            previous_was_escaped = true;
            continue;
        }

        copy[pos] = ch;
        pos++;
    }

    if (previous_was_escaped) {
        // escape character is left open, i.e. this is an uninterpretable string
        // don't return it at all, just void it
        free(copy);
        return NULL;
    }

    return copy;
}

char* copy_unescaped_string(char *s) {
    if (!s) {
        return NULL;
    }

    return copy_partial_unescaped_string(s, strlen(s));
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

int count_chars(char *s, char needle, int length) {
    if (length <= 0) {
        return 0;
    }
    
    int count = 0;
    
    while (length && s && *s) {
        if (*s == needle) {
            count++;
        }
        
        s++;
        length--;
    }

    return count;
}
