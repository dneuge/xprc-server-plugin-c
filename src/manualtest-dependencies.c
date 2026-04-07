#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dependencies.h"
#include "licenses.h"
#include "utils.h"

static int print_dependency(xprc_dependency_t *dependency, int index) {
    if (index > 0) {
        printf("#%02d ", index);
    }

    printf("dependency \"%s\"\n", dependency->id);
    printf("    name:       %s\n", dependency->name);
    printf("    version:    %s\n", dependency->version);
    printf("    url:        %s\n", dependency->url);
    printf("    active?     %s\n", dependency->active ? "yes" : "no");
    printf("    activation: %s\n", dependency->activation);
    printf("\n");

    list_t *copyrights = xprc_get_dependency_copyrights(dependency);
    if (!copyrights) {
        printf("\nERROR: failed to retrieve copyrights\n");
        return 1;
    }

    int res = 0;
    int copyright_index = 1;
    for (list_item_t *item=copyrights->head; item; item=item->next) {
        xprc_dependency_copyright_t *copyright = item->value;
        printf("    copyright #%02d\n", copyright_index++);
        printf("        license ID: %s", copyright->license_id);
        if (copyright->license_id) {
            xprc_license_t *license = xprc_get_license(copyright->license_id);
            if (license) {
                printf(" [OK: %s]", license->name);
            } else {
                printf(" [missing]");
                res = 1;
            }
        }
        printf("\n");

        printf("        remark:     ");
        char *remark = copy_string(copyright->copyright_remark);
        if (!remark) {
            printf("\nERROR: failed to copy string for copyright remark\n");
            res = 1;
        } else {
            char *line = remark;
            while (*line) {
                // search end of line and terminate with NULL
                char *eol = line;
                while (*eol!='\n' && *eol!=0) {
                    eol++;
                }
                bool eos = (*eol == 0);
                *eol = 0;

                if (line != remark) {
                    printf("                    ");
                }

                printf("%s\n", line);

                line = eol;
                if (eos) {
                    break;
                }
                line++;
            }

            free(remark);
            remark = NULL;
        }

        printf("\n");
    }

    if (res) {
        printf("\nERROR: incomplete copyright/license information\n");
    }

    destroy_list(copyrights, NULL);

    return res;
}

static int print_dependencies() {
    list_t *dependencies = xprc_get_dependencies();
    if (!dependencies) {
        printf("failed to get dependencies\n");
        return 1;
    }

    int index = 1;
    int res = 0;
    for (list_item_t *item=dependencies->head; item; item=item->next) {
        xprc_dependency_t *dependency = item->value;
        res = print_dependency(dependency, index++);
        if (res) {
            break;
        }
        printf("\n");
    }

    destroy_list(dependencies, NULL);

    return res;
}

int main(int argc, char **argv) {
    if (argc == 1) {
        return print_dependencies();
    } else if (argc > 2) {
        printf("call with no parameters or one ID to display\n");
        return 1;
    }

    list_t *dependencies = xprc_get_dependencies();
    if (!dependencies) {
        printf("failed to get dependencies\n");
        return 1;
    }

    int res = 0;

    char *dependency_id = argv[1];
    bool found = false;
    for (list_item_t *item=dependencies->head; item; item=item->next) {
        xprc_dependency_t *dependency = item->value;
        if (!strcmp(dependency_id, dependency->id)) {
            found = true;
            res = print_dependency(dependency, 0);
            break;
        }
    }

    if (!found) {
        printf("dependency not found: \"%s\"\n", dependency_id);
        res = 1;
    }

    destroy_list(dependencies, NULL);

    return res;
}