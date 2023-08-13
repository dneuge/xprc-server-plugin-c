#include <stdio.h>
#include <stdlib.h>

#include "fileio.h"

// Tests reading and writing files incl. splitting and rejoining lines through fileio.h.

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        printf("Must be called with input and optional output file path. WARNING: Output file will be overwritten!\n");
        return 1;
    }

    char *in_path = argv[1];
    char *out_path = (argc > 2) ? argv[2] : NULL;
    error_t err = ERROR_NONE;

    list_t *lines = NULL;

    printf("Reading: %s\n", in_path);
    err = read_lines_from_file(&lines, in_path);
    if (err != ERROR_NONE) {
        printf("read_lines_from_file failed: %d\n", err);
        return 1;
    }

    printf("Read %d lines:\n", lines->size);
    int line_number = 1;
    list_item_t *item = lines->head;
    while (item) {
        printf("%4d %s\n", line_number++, (char*) item->value);
        item = item->next;
    }

    if (!out_path) {
        printf("File output is disabled.\n");
    } else {
        printf("Writing: %s\n", out_path);
        err = write_lines_to_file(lines, out_path);
        if (err != ERROR_NONE) {
            printf("read_lines_from_file failed: %d\n", err);
            destroy_list(lines, free);
            return 1;
        }
    }

    destroy_list(lines, free);

    printf("Done.\n");

    return 0;
}
