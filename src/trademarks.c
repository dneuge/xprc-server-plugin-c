#include <stdio.h>

#include "trademarks.h"

static const char *_xprc_trademarks_acknowledgments[];

list_t* xprc_get_trademarks_acknowledgments() {
    list_t *out = create_list();
    if (!out) {
        return NULL;
    }

    for (char **item = (char**)_xprc_trademarks_acknowledgments; item && *item; item++) {
        if (!list_append(out, *item)) {
            goto error;
        }
    }

    goto end;

error:
    destroy_list(out, NULL);
    out = NULL;

end:
    return out;
}

#include "_trademarks.c"
