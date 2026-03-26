#ifdef TARGET_WINDOWS
#include "clipboard_windows.c"
#else

// no generic clipboard implementation possible

#include "clipboard.h"

bool is_clipboard_available() {
    return false;
}

error_t copy_plaintext_to_clipboard(char *s) {
    return ERROR_UNSPECIFIC;
}

#endif
