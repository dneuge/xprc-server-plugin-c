#ifndef XPRC_CLIPBOARD_H
#define XPRC_CLIPBOARD_H

#include <stdbool.h>

#include "../errors.h"

/**
 * Checks if a clipboard implementation is available.
 *
 * @return true if available, false if not
 */
bool is_clipboard_available();

/**
 * Copies the given null-terminated plain-text string to the desktop environment's clipboard, if supported.
 *
 * The provided string will be copied and must be memory-managed by the caller.
 *
 * @param s null-terminated plain-text string to copy
 * @return error code, #ERROR_NONE on success
 */
error_t copy_plaintext_to_clipboard(char *s);

#endif //XPRC_CLIPBOARD_H