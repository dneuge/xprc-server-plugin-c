#include <stdio.h>

#include "licenses.h"

static int print_ids() {
    printf("Available licenses:\n\n");

    list_t *ids = xprc_get_license_ids();
    if (!ids) {
        printf("failed to retrieve license IDs\n");
        return 1;
    }

    list_item_t *item = ids->head;
    while (item) {
        printf("%s\n", (char*)item->value);
        item = item->next;
    }

    destroy_list(ids, NULL);

    return 0;
}

static int print_license(char *id) {
    xprc_license_t *license = xprc_get_license(id);
    if (!license) {
        printf("unknown license: %s\n", id);
        return 1;
    }

    printf("ID:         %s\n", license->id);
    printf("Full name:  %s\n", license->name);
    printf("Short name: %s\n", license->short_name);
    printf("Hash:       %u\n", license->hash);
    printf("\n");
    printf("%s\n", license->text);

    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        return print_ids();
    } else if (argc == 2) {
        argv++;
        return print_license(*argv);
    } else {
        printf("call with single license ID to display or no parameters to list available licenses\n");
        return 1;
    }
}