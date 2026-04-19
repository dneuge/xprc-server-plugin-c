/* The goal of this file is to establish compatibility with Windows operating systems.
 *
 * This file is based on API information published by Microsoft under CC-BY 4.0 and MIT licenses at:
 *
 *   https://github.com/MicrosoftDocs/sdk-api
 *   revision 586165cc8a117fdce141de4c8a3b4bb8be9d7cae (12 Jul 2022)
 *   AI rationale: later revisions look like they may have been partially affected by AI tooling, commits up to
 *                 early July 2022 seem "normal" at first glance
 *   Deep links:
 *   https://github.com/MicrosoftDocs/sdk-api/tree/586165cc8a117fdce141de4c8a3b4bb8be9d7cae/sdk-api-src/content/winuser
 *
 *
 *   https://github.com/MicrosoftDocs/win32
 *   license checked at revision 192df369a6bdb7d290ddb47187c8371507dc6c0a (20 Mar 2026)
 *   see individual revisions/AI rationales below
 *
 *   see repositories at specified revisions for detailed license information
 *
 * Official API documentation omits headers and thus low-level type information. Missing information has
 * been substituted in reference to headers distributed as part of wine which are published under terms of
 * LGPL 2.1:
 *
 *   https://github.com/wine-mirror/wine/blob/master/include/
 *
 * This file itself remains published under MIT license. If one of the API reference sources requires a more
 * restrictive license to be put into effect, the respective license shall take precedence with closely limited effect
 * in accordance to the right to sublicense MIT-licensed works. This will mainly affect binary distributions and
 * other code that might be based upon this file. To avoid licensing issues and to avoid taking over a potentially
 * wrong implementation, it is strongly recommended not to use this file for reference in other projects; follow
 * the original API docs on your own instead.
 */

#include <string.h>

// Windows API
#ifdef TARGET_WINDOWS
#include <windows.h>
#else
#include "../windows_stub.h"
#endif

#include "../logger.h"

#include "clipboard.h"

bool is_clipboard_available() {
    return true;
}

error_t copy_plaintext_to_clipboard(char *s) {
    /* Based on documentation and examples from:
     *
     * https://stackoverflow.com/a/1264179
     * licensed CC-BY-SA 4.0, author Judge Maygarden, 12 Aug 2009
     *
     * https://github.com/MicrosoftDocs/win32/blob/34ebbe3e55bf07700918cee1452aa0d35f9dff01/desktop-src/dataxchg/using-the-clipboard.md
     * Committed on 19 Nov 2022
     * AI rationale: minor doubts about later edits, using 2022 revision instead which shows no signs of AI involvement
     */

    error_t out_error = ERROR_NONE;
    bool clipboard_open = false;
    bool memory_handle_locked = false;

    RCLOG_DEBUG("copy_plaintext_to_clipboard: entered");

    if (!s) {
        RCLOG_WARN("copy_plaintext_to_clipboard: called with NULL");
        return ERROR_UNSPECIFIC;
    }

    // TODO: error information would be available through GetLastError, if needed

    size_t length = strlen(s);

    // memory must be movable according to clipboard documentation
    // GlobalAlloc returns a handle, not a directly usable pointer
    RCLOG_DEBUG("copy_plaintext_to_clipboard: allocating memory");
    HGLOBAL memory_handle = GlobalAlloc(GMEM_MOVEABLE, length+1);
    if (!memory_handle) {
        RCLOG_WARN("copy_plaintext_to_clipboard: failed to allocate memory");
        out_error = ERROR_MEMORY_ALLOCATION;
        goto error;
    }

    RCLOG_DEBUG("copy_plaintext_to_clipboard: locking memory handle for write-access");
    char *memory = GlobalLock(memory_handle);
    memory_handle_locked = (memory != NULL); // used in error handling
    if (!memory_handle_locked) {
        RCLOG_WARN("copy_plaintext_to_clipboard: failed to lock memory handle for write-access");
        goto error;
    }

    RCLOG_DEBUG("copy_plaintext_to_clipboard: copying contents");
    memcpy(memory, s, length);
    memory[length] = 0;

    GlobalUnlock(memory_handle);
    if (GetLastError() != NO_ERROR) {
        RCLOG_WARN("copy_plaintext_to_clipboard: failed to unlock memory handle after write-access");
        goto error;
    }
    memory = NULL; // we no longer have access
    memory_handle_locked = false;

    // we can pass NULL if we don't have a specific handle to operate on which should act in the context of the "current task"
    // https://github.com/MicrosoftDocs/sdk-api/blob/586165cc8a117fdce141de4c8a3b4bb8be9d7cae/sdk-api-src/content/winuser/nf-winuser-openclipboard.md
    if (!OpenClipboard(NULL)) {
        RCLOG_WARN("copy_plaintext_to_clipboard: failed to open clipboard");
        goto error;
    }
    clipboard_open = true;

    if (!EmptyClipboard()) {
        RCLOG_WARN("copy_plaintext_to_clipboard: failed to empty clipboard");
        goto error;
    }

    HGLOBAL clipboard_data_handle = SetClipboardData(CF_TEXT, memory_handle);
    if (!clipboard_data_handle) {
        RCLOG_WARN("copy_plaintext_to_clipboard: clipboard did not accept memory handle");
        goto error;
    } else if (clipboard_data_handle != memory_handle) {
        RCLOG_WARN("copy_plaintext_to_clipboard: memleak? clipboard seems to hold an unexpected memory handle");
    }

    // memory has been taken over by clipboard, we need to forget the handle
    memory_handle = NULL;

    goto end;

error:
    if (out_error == ERROR_NONE) {
        out_error = ERROR_UNSPECIFIC;
    }

    if (memory_handle) {
        // documentation reads as if we need to lock the handle before we can call GlobalFree
        if (!memory_handle_locked) {
            RCLOG_DEBUG("copy_plaintext_to_clipboard: locking memory handle before freeing (error handling)");
            memory_handle_locked = !GlobalLock(memory_handle);
        }

        if (!memory_handle_locked) {
            RCLOG_WARN("copy_plaintext_to_clipboard: memleak - failed to lock memory handle to free during error handling");
        } else {
            RCLOG_DEBUG("copy_plaintext_to_clipboard: freeing memory handle (error handling)");

            // GlobalFree returns NULL if successful
            if (GlobalFree(memory_handle)) {
                RCLOG_WARN("copy_plaintext_to_clipboard: memleak - failed to free memory handle during error handling");
            } else {
                // successfully freed - forget memory handle incl. lock which should have been revoked
                memory_handle_locked = false;
                memory_handle = NULL;
            }

            // release lock if still held
            if (memory_handle_locked) {
                GlobalUnlock(memory_handle);
                if (GetLastError() != NO_ERROR) {
                    RCLOG_WARN("copy_plaintext_to_clipboard: failed to unlock memory handle (error handling)");
                }
            }
        }
    }

end:
    if (clipboard_open) {
        CloseClipboard();
        if (GetLastError() != NO_ERROR) {
            RCLOG_ERROR("copy_plaintext_to_clipboard: failed to close clipboard... no idea what this means and what's happening next. good luck!");
        }
        clipboard_open = false;
    }

    RCLOG_DEBUG("copy_plaintext_to_clipboard: done");

    return out_error;
}
