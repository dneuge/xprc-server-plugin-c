#include <stdio.h>
#include <stdlib.h>

#include "fileio.h"
#include "logger.h"

// Tests reading and writing files incl. splitting and rejoining lines through fileio.h.

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        printf("Must be called with input and optional output file path. WARNING: Output file will be overwritten!\n");
        return 1;
    }

    xprc_log_init();
    xprc_set_min_log_level_console(RCLOG_LEVEL_TRACE);

    char *in_path = argv[1];
    char *out_path = (argc > 2) ? argv[2] : NULL;
    error_t err = ERROR_NONE;

    list_t *lines = NULL;

    RCLOG_INFO("Reading: %s", in_path);
    err = read_lines_from_file(&lines, in_path);
    if (err != ERROR_NONE) {
        RCLOG_WARN("read_lines_from_file failed: %d", err);
        return 1;
    }

    RCLOG_INFO("Read %d lines:", lines->size);
    int line_number = 1;
    list_item_t *item = lines->head;
    while (item) {
        RCLOG_INFO("%4d %s", line_number++, (char*) item->value);
        item = item->next;
    }

    if (!out_path) {
        RCLOG_INFO("File output is disabled.");
    } else {
        RCLOG_INFO("Writing: %s", out_path);
        err = write_lines_to_file(lines, out_path);
        if (err != ERROR_NONE) {
            RCLOG_WARN("write_lines_to_file failed: %d", err);
            destroy_list(lines, free);
            return 1;
        }
    }

    destroy_list(lines, free);

    RCLOG_INFO("Done.");

    xprc_log_destroy();

    return 0;
}
