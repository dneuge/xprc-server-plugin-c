#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logger.h"
#include "utils.h"

#include "fileio.h"
#include "fileio_internal.h"

#if defined(TARGET_LINUX) || defined(TARGET_MACOS)
#include "fileio_standard.c"
#elif TARGET_WINDOWS
#include "fileio_windows.c"
#else
#error "File I/O is target-specific but has not been implemented for the requested platform."
#endif

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
    file_handle_t fh = {0,};
    char *buffer = NULL;
    error_t err = ERROR_NONE;
    error_t out_err = ERROR_NONE;
    list_t *list = NULL;

    if (!data || !length || !path) {
        RCLOG_WARN("[fileio] read_file misses input: data=%p, length=%p, path=%s", data, length, path);
        return ERROR_UNSPECIFIC;
    }

    err = init_file_handle(&fh);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[fileio] read_file failed to initialize file handle");
        return err;
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

    err = open_file(&fh, FILE_MODE_READ, path);
    if (err != ERROR_NONE) {
        out_err = err;
        goto end;
    }

    size_t num_read = 0;
    size_t total_length = 0;

    while (true) {
        // try to read as many characters as possible
        out_err = read_bytes(&num_read, &fh, buffer, BUFFER_SIZE);
        if (num_read == 0 || out_err != ERROR_NONE) {
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
    if (out_err != ERROR_NONE) {
        goto end;
    } else if (!check_eof(&fh)) {
        RCLOG_WARN("[fileio] read_file did not read until EOF (got total_length=%zu): %s", total_length, path);
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
    if (is_open_file(&fh)) {
        err = close_file(&fh);
        if (err != ERROR_NONE && out_err == ERROR_NONE) {
            out_err = err;
        }
    }

    if (list) {
        destroy_list(list, destroy_segment);
    }

    if (buffer) {
        free(buffer);
    }

    if (out_err != ERROR_NONE) {
        RCLOG_WARN("[fileio] failed reading from file %s (%d)", path, out_err);
    }

    return out_err;
}

error_t write_file(char *data, size_t length, char *path) {
    file_handle_t fh = {0,};
    error_t err = ERROR_NONE;
    error_t out_err = ERROR_NONE;

    if (!data || !path) {
        RCLOG_WARN("[fileio] write_file misses input: data=%p, length=%zu, path=%s", data, length, path);
        return ERROR_UNSPECIFIC;
    }

    err = init_file_handle(&fh);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[fileio] write_file failed to initialize file handle");
        return err;
    }

    err = open_file(&fh, FILE_MODE_WRITE, path);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[fileio] write_file failed to open file for write access: %s", path);
        return err;
    }

    size_t num_written = 0;
    out_err = write_bytes(&num_written, &fh, data, length);

    err = close_file(&fh);
    if (err != ERROR_NONE && out_err == ERROR_NONE) {
        out_err = err;
    }

    if (num_written != length) {
        RCLOG_WARN("[fileio] invalid number of bytes written to %s, expected %zu, wrote %zu", path, length, num_written);
        if (out_err == ERROR_NONE) {
            out_err = ERROR_UNSPECIFIC;
        }
    }

    if (out_err != ERROR_NONE) {
        RCLOG_WARN("[fileio] failed writing to file %s (%d)", path, out_err);
    }

    return out_err;
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

error_t read_first_line_from_file(char **line, char *path) {
    error_t out_err = ERROR_NONE;

    if (!line || !path) {
        RCLOG_WARN("[fileio] read_first_line_from_file missing parameters: line=%p, path=%p", line, path);
        return ERROR_UNSPECIFIC;
    }

    if (*line) {
        RCLOG_WARN("[fileio] read_first_line_from_file called with output variable still pointing to %p - refusing to produce potential memleak", *line);
        return ERROR_UNSPECIFIC;
    }

    list_t *lines = NULL;
    error_t err = read_lines_from_file(&lines, path);
    if (err != ERROR_NONE) {
        RCLOG_WARN("[fileio] read_first_line_from_file failed to read %s", path);
        return err;
    }

    if (!lines || lines->size < 1) {
        out_err = ERROR_UNSPECIFIC;
        goto end;
    }

    // extract the line from the result list; must remain available after destroying the list, so we have to clear the
    // reference (all other lines will be freed)
    *line = lines->head->value;
    lines->head->value = NULL;

    if (!*line) {
        RCLOG_WARN("[fileio] read_first_line_from_file read NULL line from %s", path);
        out_err = ERROR_UNSPECIFIC;
    }

end:
    if (lines) {
        destroy_list(lines, free);
    }

    return out_err;
}
