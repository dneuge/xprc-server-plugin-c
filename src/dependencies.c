#include <stdio.h>

#include "dependencies.h"

static const xprc_dependency_t _xprc_dependencies[];

list_t* xprc_get_dependencies() {
    list_t *out = create_list();
    if (!out) {
        return NULL;
    }

    xprc_dependency_t *item = (xprc_dependency_t*) _xprc_dependencies;
    while (item->id) {
        if (!list_append(out, item)) {
            destroy_list(out, NULL);
            return NULL;
        }

        item++;
    }

    return out;
}

list_t* xprc_get_dependency_copyrights(xprc_dependency_t *dependency) {
    if (!dependency) {
        return NULL;
    }

    list_t *out = create_list();
    if (!out) {
        return NULL;
    }

    xprc_dependency_copyright_t *item = dependency->_copyrights;
    while (item->copyright_remark) {  // license_id can be NULL, copyright_remark must be checked for
        if (!list_append(out, item)) {
            destroy_list(out, NULL);
            return NULL;
        }

        item++;
    }

    return out;
}

#include "_dependencies.c"
