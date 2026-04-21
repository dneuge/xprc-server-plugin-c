#ifndef XPRC_LICENSES_H
#define XPRC_LICENSES_H

#include <stdint.h>

#include "lists.h"

#define XPRC_BINARY_LICENSE_ID "_xprc-binary"
#define XPRC_SOURCE_LICENSE_ID "MIT"

#define XPRC_LICENSE_ACCEPTANCE_TEXT "I have read and accept ALL licenses and associated disclaimers"

typedef uint32_t xprc_license_hash_t;

typedef struct {
    char *id;
    char *name;
    char *short_name;
    char *text;
    xprc_license_hash_t hash;
} xprc_license_t;

/**
 * Parses a license hash from given string.
 *
 * @param out will be set to read hash, if successful
 * @param s string to parse
 * @return true if successful, false on error
 */
bool xprc_parse_license_hash(xprc_license_hash_t *out, char *s);

/**
 * Formats the given hash to a string representation that can be read by #xprc_parse_license_hash again.
 *
 * Caller has to manage the returned string.
 *
 * @param hash hash to be formatted
 * @return null-terminated string representing the hash; NULL on error
 */
char* xprc_format_license_hash(xprc_license_hash_t hash);

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