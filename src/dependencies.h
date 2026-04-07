#ifndef XPRC_DEPENDENCIES_H
#define XPRC_DEPENDENCIES_H

#include <stdbool.h>

#include "lists.h"

typedef struct {
    char *license_id; // may be omitted by NULL
    char *copyright_remark;
} xprc_dependency_copyright_t;

typedef struct {
    char *id;
    char *name;
    char *version; // may be omitted by NULL
    char *url; // may be omitted by NULL

    bool active;
    char *activation; // may be omitted by NULL

    // some dependencies apply different licenses to different parts, also depending on date & author, so a single
    // copyright notice/license link is not sufficient
    xprc_dependency_copyright_t *_copyrights;
} xprc_dependency_t;

/**
 * Creates a new list containing references to all dependency definitions
 * (see #xprc_dependency_t).
 *
 * Memory management:
 * - the list structure is owned and must be managed by the caller
 * - list values are constants shared from program binary and
 *   must not be manipulated or freed
 *
 * @return new list holding shared dependency information; NULL on error
 */
list_t* xprc_get_dependencies();

/**
 * Creates a new list containing references to all copyright descriptions
 * of the given dependency (see #xprc_dependency_copyright_t).
 *
 * Memory management:
 * - the list structure is owned and must be managed by the caller
 * - list values are constants shared from program binary and
 *   must not be manipulated or freed
 *
 * @param dependency dependency to list copyrights for
 * @return new list holding shared copyright references; NULL on error
 */
list_t* xprc_get_dependency_copyrights(xprc_dependency_t *dependency);

#endif //XPRC_DEPENDENCIES_H