#ifndef XPRC_TRADEMARKS_H
#define XPRC_TRADEMARKS_H

#include "lists.h"


/**
 * Creates a new list containing references to all trademark and acknowledgment strings.
 *
 * Memory management:
 * - the list structure is owned and must be managed by the caller
 * - list values are constants shared from program binary and
 *   must not be manipulated or freed
 *
 * @return new list holding shared trademark and acknowledgment texts; NULL on error
 */
list_t* xprc_get_trademarks_acknowledgments();

#endif //XPRC_TRADEMARKS_H