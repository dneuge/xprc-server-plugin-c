#ifndef XPRC_LICENSES_H
#define XPRC_LICENSES_H

#include <stdint.h>

#include "lists.h"

typedef struct {
    char *id;
    char *name;
    char *text;
    uint32_t hash;
} xprc_license_t;

/**
 * Creates a new list containing the IDs of all available licenses.
 *
 * Memory management:
 * - the list structure is owned and must be managed by the caller
 * - list values are constants shared from program binary and
 *   must not be manipulated or freed
 *
 * @return new list holding shared license IDs; NULL on error
 */
list_t* xprc_get_license_ids();

/**
 * Returns all information about the specified license.
 *
 * The returned structure and content references are shared from
 * program binary and must not be manipulated or freed.
 *
 * @param id license ID to look up
 * @return shared reference to license; NULL if not found
 */
xprc_license_t* xprc_get_license(char *id);

#endif //XPRC_LICENSES_H