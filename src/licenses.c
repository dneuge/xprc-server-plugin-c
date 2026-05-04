#include <string.h>

#include "utils.h"

#include "licenses.h"

static const xprc_license_t _xprc_licenses[];

bool xprc_parse_license_hash(xprc_license_hash_t *out, char *s) {
    return parse_uint32(out, s);
}

char* xprc_format_license_hash(xprc_license_hash_t hash) {
    return dynamic_sprintf(UINT32_FORMAT, hash);
}

list_t* xprc_get_license_ids() {
    list_t *out = create_list();
    if (!out) {
        return NULL;
    }

    xprc_license_t *item = (xprc_license_t*) _xprc_licenses;
    while (item->id) {
        if (!list_append(out, item->id)) {
            destroy_list(out, NULL);
            return NULL;
        }

        item++;
    }

    return out;
}

xprc_license_t* xprc_get_license(char *id) {
    if (!id) {
        return NULL;
    }

    xprc_license_t *item = (xprc_license_t*) _xprc_licenses;
    while (item->id) {
        if (!strcmp(id, item->id)) {
            return item;
        }

        item++;
    }

    return NULL;
}

#include "_licenses.c"
