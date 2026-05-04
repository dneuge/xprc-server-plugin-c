#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"

#ifdef _MSC_VER
// MSVC linker fails to find inline methods, disable modifier
#define MAY_INLINE
#else
#define MAY_INLINE inline
#endif

#define MIN_YEAR (1900)
#define MAX_YEAR (2100)

char* dynamic_sprintf(char *format, ...) {
    if (!format) {
        return NULL;
    }

    va_list args;
    va_start(args, format);
    char *out = dynamic_vsprintf(format, args);
    va_end(args);

    return out;
}

char* dynamic_vsprintf(char *format, va_list args) {
    if (!format) {
        return NULL;
    }

    va_list args_copy;
    va_copy(args_copy, args);
    int required_size = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);
    
    if (required_size < 1) {
        return NULL;
    }

    int out_size = required_size + 1;
    char *out = zmalloc(out_size);
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

void* zmalloc(size_t size) {
    // prevent zero allocation requests as some malloc implementations may corrupt in that case
    if (size == 0) {
        return NULL;
    }

    void *addr = malloc(size);
    if (addr) {
        memset(addr, 0, size);
    }
    return addr;
}

void* copy_memory(void *src, size_t size) {
    if (!src) {
        return NULL;
    }

    if (size <= 0) {
        return NULL;
    }

    void *out = malloc(size);
    if (!out) {
        return NULL;
    }

    memcpy(out, src, size);

    return out;
}

char* copy_partial_string(char *s, size_t length) {
    if (!s) {
        return NULL;
    }

    char *copy = malloc(length + 1);
    if (!copy) {
        return NULL;
    }

    if (length > 0) {
        memcpy(copy, s, length);
    }
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
    if (!s) {
        return NULL;
    }

    char *copy = zmalloc(max_length + 1);
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
    if (!ts || ts->tv_sec < 0) {
        return -1;
    }
    
    return ((int64_t) ts->tv_sec * 1000) + (ts->tv_nsec / 1000000);
}

bool timespec_now_plus_millis(struct timespec *out, int base, uint16_t millis) {
    if (!out) {
        return false;
    }

    if (millis < 0) {
        return false;
    }

    if (!timespec_get(out, base)) {
        return false;
    }

    if (millis != 0) {
        out->tv_nsec += millis * 1000;
        out->tv_sec += out->tv_nsec / 1000000000;
        out->tv_nsec = out->tv_nsec % 1000000000;
    }

    return true;
}

int count_chars(char *s, char needle, int length) {
    if (!s) {
        // FIXME: should warn somehow
        return 0;
    }

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

bool parse_int(int *dest, char *s) {
    bool success = false;

    if (!dest || !s) {
        return false;
    }

    int parsed = atoi(s);

    char *verification = dynamic_sprintf("%d", parsed);
    if (!verification) {
        return false;
    }

    if (!strcmp(s, verification)) {
        // value matches; parsing successful
        *dest = parsed;
        success = true;
    }

    free(verification);

    return success;
}

bool parse_long(long *dest, char *s) {
    bool success = false;

    if (!dest || !s) {
        return false;
    }

    long parsed = atol(s);

    char *verification = dynamic_sprintf("%ld", parsed);
    if (!verification) {
        return false;
    }

    if (!strcmp(s, verification)) {
        // value matches; parsing successful
        *dest = parsed;
        success = true;
    }

    free(verification);

    return success;
}

bool parse_longlong(long long *dest, char *s) {
    bool success = false;

    if (!dest || !s) {
        return false;
    }

    long long parsed = atoll(s);

    char *verification = dynamic_sprintf("%lld", parsed);
    if (!verification) {
        return false;
    }

    if (!strcmp(s, verification)) {
        // value matches; parsing successful
        *dest = parsed;
        success = true;
    }

    free(verification);

    return success;
}

bool parse_uint32(uint32_t *dest, char *s) {
    if (!dest) {
        return false;
    }

    long long parsed = -1;
    if (!parse_longlong(&parsed, s)) {
        return false;
    }

    if (parsed < 0 || parsed > UINT32_MAX) {
        return false;
    }

    *dest = (uint32_t) parsed;

    return true;
}

static const xprc_date_t invalid_date = {
    .year = -1,
    .month = -1,
    .day = -1,
};

static bool is_digit(const char ch) {
    return (ch >= '0') && (ch <= '9');
}

bool is_valid_date(xprc_date_t *date) {
    if (!date) {
        return false;
    }

    if (date->year < MIN_YEAR || date->year > MAX_YEAR) {
        return false;
    }

    if (date->month < 1 || date->month > 12) {
        return false;
    }

    int max_days = 31;
    if (date->month == 2) {
        max_days = 28;
        if (date->year % 4 == 0) {
            max_days = 29;
        }
    } else if (date->month == 4 || date->month == 6 || date->month == 9 || date->month == 11) {
        max_days = 30;
    }

    if (date->day < 1 || date->day > max_days) {
        return false;
    }

    return true;
}

xprc_date_t parse_iso_date(char *s) {
    xprc_date_t out = invalid_date;

    char *copy = copy_string(s);
    if (!copy) {
        return invalid_date;
    }

    size_t length = strlen(copy);
    if (length < 10) {
        goto error;
    }

    bool valid_pattern = is_digit(copy[0]) && is_digit(copy[1]) && is_digit(copy[2]) && is_digit(copy[3])
        && (copy[4] == '-') && is_digit(copy[5]) && is_digit(copy[6])
        && (copy[7] == '-') && is_digit(copy[8]) && is_digit(copy[9]);
    if (length > 10) {
        valid_pattern &= (copy[10] == 'T');
    }
    if (!valid_pattern) {
        goto error;
    }

    copy[4] = 0;
    copy[7] = 0;
    copy[10] = 0;

    // skip leading zeros, otherwise parse_int will fail (not matching string format check)
    int month_offset = 5;
    if (copy[month_offset] == '0') {
        month_offset++;
    }

    int day_offset = 8;
    if (copy[day_offset] == '0') {
        day_offset++;
    }

    if (!parse_int(&out.year, &copy[0]) || !parse_int(&out.month, &copy[month_offset]) || !parse_int(&out.day, &copy[day_offset])) {
        goto error;
    }

    if (!is_valid_date(&out)) {
        goto error;
    }

    goto end;

error:
    out = invalid_date;

end:
    if (copy) {
        free(copy);
    }

    return out;
}

#ifndef HAVE_PTHREAD
void set_current_thread_name(char *format, ...) {
    // do nothing without POSIX threads
    // TODO: probably possible on Windows somehow?
}
#else
#include <pthread.h>
#define MAX_THREAD_NAME_LENGTH 15

void set_current_thread_name(char *format, ...) {
    va_list args;
    va_start(args, format);
    char *name = dynamic_vsprintf(format, args);
    va_end(args);

    if (!name) {
        return;
    }

    int len = strlen(name);
    if (len > MAX_THREAD_NAME_LENGTH) {
        name[len] = 0;
    }

    pthread_setname_np(pthread_self(), name);

    free(name);
}
#endif