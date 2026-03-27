#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logger.h"
#include "utils.h"

#include "fileio.h"

#define BUFFER_SIZE (64 * 1024)

typedef struct {
    char *data;
    size_t length;
} segment_t;

static segment_t* create_segment(char *data, size_t length) {
    segment_t *segment = zmalloc(sizeof(segment_t));
    if (!segment) {
        return NULL;
    }

    segment->data = data;
    segment->length = length;

    return segment;
}

static void destroy_segment(void *value) {
    segment_t *segment = value;
    if (!segment) {
        return;
    }

    if (segment->data) {
        free(segment->data);
    }

    free(segment);
}

error_t split_lines(list_t **lines, char *s, size_t length) {
    if (!lines || !s) {
        goto error;
    }

    *lines = create_list();
    if (!(*lines)) {
        goto error;
    }

    size_t start_of_line = 0;
    for (size_t i=0; i<length; i++) {
        char ch = s[i];

        if ((ch == '\r') || (ch == '\n')) {
            // line is being terminated, copy string
            char *line = copy_partial_string(&s[start_of_line], i-start_of_line);
            if (!line) {
                goto error;
            }

            if (!list_append(*lines, line)) {
                free(line);
                goto error;
            }

            start_of_line = i+1;
        }

        // if we have a sequence \r\n we should skip the next character as well
        if ((ch == '\r') && (i < (length-1)) && (s[i+1] == '\n')) {
            i++;
            start_of_line++;
        }
    }

    // add the final line; if the file ended in a line-break the last line will be empty
    char *line = copy_partial_string(&s[start_of_line], length - start_of_line);
    if (!line) {
        goto error;
    }

    if (!list_append(*lines, line)) {
        free(line);
        goto error;
    }

    return ERROR_NONE;

error:
    if (lines && *lines) {
        destroy_list(*lines, free);
        *lines = NULL;
    }
    return ERROR_MEMORY_ALLOCATION;
}

char* join_lines(list_t *lines) {
    if (!lines) {
        return NULL;
    }

    size_t total_length = 0;
    list_item_t *item = lines->head;
    while (item) {
        total_length += strlen(item->value);

        if (item->next) {
            total_length += LINE_END_SEQUENCE_LENGTH;
        }

        item = item->next;
    }

    char *out = zmalloc(total_length + 1);
    if (!out) {
        return NULL;
    }

    size_t offset = 0;
    item = lines->head;
    while (item) {
        size_t length = strlen(item->value);
        if (length > 0) {
            memcpy(&out[offset], item->value, length);
            offset += length;
        }

        if (item->next) {
            memcpy(&out[offset], LINE_END_SEQUENCE, LINE_END_SEQUENCE_LENGTH);
            offset += LINE_END_SEQUENCE_LENGTH;
        }

        item = item->next;
    }
    out[offset] = 0;

    return out;
}

error_t read_file(char **data, size_t *length, char *path) {
    FILE* fh = NULL;
    char *buffer = NULL;
    error_t out_err = ERROR_NONE;
    list_t *list = NULL;

    if (!data || !length || !path) {
        RCLOG_WARN("[fileio] read_file misses input: data=%p, length=$lu, path=%s", data, length, path);
        return ERROR_UNSPECIFIC;
    }

    *data = NULL;
    *length = 0;

    buffer = zmalloc(BUFFER_SIZE);
    if (!buffer) {
        out_err = ERROR_MEMORY_ALLOCATION;
        goto end;
    }

    list = create_list();
    if (!list) {
        out_err = ERROR_MEMORY_ALLOCATION;
        goto end;
    }

    fh = fopen(path, "rb");
    if (!fh) {
        out_err = ERROR_UNSPECIFIC;
        goto end;
    }

    size_t num_read = 0;
    size_t total_length = 0;

    while (true) {
        // try to read as many characters as possible
        num_read = fread(buffer, 1, BUFFER_SIZE, fh);
        if (num_read == 0) {
            break;
        }

        char *copy = copy_memory(buffer, num_read);
        if (!copy) {
            out_err = ERROR_MEMORY_ALLOCATION;
            goto end;
        }

        segment_t *segment = create_segment(copy, num_read);
        if (!segment) {
            free(copy);
            out_err = ERROR_MEMORY_ALLOCATION;
            goto end;
        }

        if (!list_append(list, segment)) {
            destroy_segment(segment);
            out_err = ERROR_MEMORY_ALLOCATION;
            goto end;
        }

        total_length += num_read;
    }

    // we expect to have read until EOF; if we didn't make it there we have encountered a problem
    if (!feof(fh)) {
        out_err = ERROR_UNSPECIFIC;
        goto end;
    }

    // concatenate all segments and terminate with null
    *data = malloc(total_length + 1);
    if (!(*data)) {
        out_err = ERROR_MEMORY_ALLOCATION;
        goto end;
    }

    list_item_t *item = list->head;
    size_t offset = 0;
    while (item) {
        segment_t *segment = item->value;

        memcpy(&((*data)[offset]), segment->data, segment->length);
        offset += segment->length;

        item = item->next;
    }

    (*data)[total_length] = 0;
    *length = total_length;

end:
    if (fh) {
        fclose(fh);
    }

    if (list) {
        destroy_list(list, destroy_segment);
    }

    if (buffer) {
        free(buffer);
    }

    return out_err;
}

error_t write_file(char *data, size_t length, char *path) {
    if (!data || !path) {
        RCLOG_WARN("[fileio] write_file misses input: data=%p, length=$lu, path=%s", data, length, path);
        return ERROR_UNSPECIFIC;
    }

    FILE *fh = fopen(path, "w");
    if (!fh) {
        RCLOG_WARN("[fileio] write_file failed to open file for write access: %s", path);
        return ERROR_UNSPECIFIC;
    }

    size_t num_written = fwrite(data, 1, length, fh);

    fclose(fh);

    if (num_written == length) {
        return ERROR_NONE;
    } else {
        RCLOG_WARN("[fileio] invalid number of bytes written to %s, expected %lu, wrote %lu", path, length, num_written);
        return ERROR_UNSPECIFIC;
    }
}

error_t read_lines_from_file(list_t **lines, char *path) {
    char *data = NULL;
    size_t length = 0;
    error_t err = ERROR_NONE;

    if (!lines || !path) {
        RCLOG_WARN("[fileio] read_lines_from_file misses input: lines=%p, path=%s", lines, path);
        return ERROR_UNSPECIFIC;
    }

    *lines = NULL;

    err = read_file(&data, &length, path);
    if (err != ERROR_NONE) {
        return err;
    }

    err = split_lines(lines, data, length);
    free(data);
    if (err != ERROR_NONE) {
        return err;
    }

    return ERROR_NONE;
}

error_t write_lines_to_file(list_t *lines, char *path) {
    if (!lines || !path) {
        RCLOG_WARN("[fileio] write_lines_to_file misses input: lines=%p, path=%s", lines, path);
        return ERROR_UNSPECIFIC;
    }

    char *s = join_lines(lines);
    if (!s) {
        RCLOG_WARN("[fileio] failed to join lines");
        return ERROR_MEMORY_ALLOCATION;
    }

    error_t err = write_file(s, strlen(s), path);
    free(s);
    return err;
}

#if defined(TARGET_LINUX) || defined(TARGET_MACOS)
#include <unistd.h>
#include <errno.h>
bool check_file_exists(char *path) {
    if (!path) {
        RCLOG_ERROR("[fileio] check_file_exists called without path; unpredictable behaviour (indicating file would not exist)");
        return false;
    }

    if (access(path, F_OK) == 0) {
        return true;
    }

    return (errno != ENOENT);
}
#elif TARGET_WINDOWS
#include <windows.h>
#include <winnls.h>

#define USE_NULL_TERMINATION (-1)

bool check_file_exists(char *path) {
    /* The goal of this function is to establish compatibility with Windows operating systems.
     *
     * This function is based on API information published by Microsoft under CC-BY 4.0 and MIT licenses at:
     *
     *   https://github.com/MicrosoftDocs/sdk-api
     *   revision 5da3012685fee3b1dbbefe7fa1f9a9935b9fa14e (2 Aug 2024)
     *
     *   https://github.com/MicrosoftDocs/win32
     *   revision 7d616e305727028f71d325dd5c411c0f04c964de (2 Aug 2024)
     *
     *   see repositories at specified revisions for detailed license information
     *
     * Official API documentation omits headers and thus low-level type information. Missing information has
     * been substituted in reference to headers distributed as part of wine which are published under terms of
     * LGPL 2.1:
     *
     *   https://github.com/wine-mirror/wine/blob/master/include/
     *
     * This file itself remains published under MIT license. If one of the API reference sources requires a more
     * restrictive license to be put into effect, the respective license shall take precedence with closely limited effect
     * in accordance to the right to sublicense MIT-licensed works. This will mainly affect binary distributions and
     * other code that might be based upon this file. To avoid licensing issues and to avoid taking over a potentially
     * wrong implementation, it is strongly recommended not to use this file for reference in other projects; follow
     * the original API docs on your own instead.
     *
     * [sdk-api] docs/sdk-api-src/content/fileapi/nf-fileapi-getfileattributesw.md
     * [sdk-api] docs/sdk-api-src/content/stringapiset/nf-stringapiset-multibytetowidechar.md
     * [win32]   docs/desktop-src/Intl/code-page-identifiers.md
     */

    if (!path) {
        RCLOG_ERROR("[fileio] check_file_exists called without path; unpredictable behaviour (indicating file would not exist)");
        return false;
    }

    // prepend \\?\ to enable long path handling
    char *long_path = dynamic_sprintf("\\\\?\\%s", path);
    if (!long_path) {
        RCLOG_WARN("failed to construct long path from: \"%s\"", path);
        return false;
    }

    int buffer_length = MultiByteToWideChar(
        /* CodePage       */ CP_UTF8,
        /* dwFlags        */ MB_ERR_INVALID_CHARS,
        /* lpMultiByteStr */ long_path,
        /* cbMultiByte    */ USE_NULL_TERMINATION,
        /* lpWideCharStr  */ NULL,
        /* cchWideChar    */ 0 // calculate required buffer size only
    );

    if (buffer_length <= 0) {
        unsigned long err = GetLastError();
        RCLOG_WARN("string conversion failed early with error %lu: \"%s\"", err, long_path);
        free(long_path);
        return false;
    }

    size_t buffer_size = buffer_length * sizeof(WCHAR);

    WCHAR *mb_long_path = zmalloc(buffer_size);
    if (!mb_long_path) {
        RCLOG_WARN("failed to allocate %lu bytes for path string conversion", buffer_size);
        free(long_path);
        return false;
    }

    int res = MultiByteToWideChar(
        /* CodePage       */ CP_UTF8,
        /* dwFlags        */ MB_ERR_INVALID_CHARS,
        /* lpMultiByteStr */ long_path,
        /* cbMultiByte    */ USE_NULL_TERMINATION,
        /* lpWideCharStr  */ mb_long_path,
        /* cchWideChar    */ buffer_length
    );

    if (res <= 0) {
        unsigned long err = GetLastError();
        RCLOG_WARN("string conversion failed late with error %lu: \"%s\"", err, long_path);
        free(mb_long_path);
        free(long_path);
        return false;
    }

    long attributes = GetFileAttributesW(mb_long_path);

    free(mb_long_path);
    free(long_path);

    return attributes != INVALID_FILE_ATTRIBUTES;
}
#else
#error "Check for file existence is target-specific but has not been implemented for the requested platform."
#endif
