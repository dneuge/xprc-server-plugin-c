#ifdef TARGET_WINDOWS
#include <windows.h>
#include <winnls.h>
#else
#include "windows_stub.h"
#endif

#include <stdlib.h>

#include "logger.h"

#include "fileio.h"

#define USE_NULL_TERMINATION (-1)

bool check_file_exists(char *path) {
    /* The goal of this function is to establish compatibility with Windows operating systems.
     *
     * This function is based on API information published by Microsoft under CC-BY 4.0 and MIT licenses at:
     *
     *   https://github.com/MicrosoftDocs/sdk-api
     *   revision 5da3012685fee3b1dbbefe7fa1f9a9935b9fa14e (2 Aug 2024)
     *
     *   https://github.com/MicrosoftDocs/win32
     *   revision 7d616e305727028f71d325dd5c411c0f04c964de (2 Aug 2024)
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
     *
     * [sdk-api] docs/sdk-api-src/content/fileapi/nf-fileapi-getfileattributesw.md
     * [sdk-api] docs/sdk-api-src/content/stringapiset/nf-stringapiset-multibytetowidechar.md
     * [win32]   docs/desktop-src/Intl/code-page-identifiers.md
     */

    if (!path) {
        RCLOG_ERROR("[fileio] check_file_exists called without path; unpredictable behaviour (indicating file would not exist)");
        return false;
    }

    // prepend \\?\ to enable long path handling
    char *long_path = dynamic_sprintf("\\\\?\\%s", path);
    if (!long_path) {
        RCLOG_WARN("failed to construct long path from: \"%s\"", path);
        return false;
    }

    int buffer_length = MultiByteToWideChar(
        /* CodePage       */ CP_UTF8,
        /* dwFlags        */ MB_ERR_INVALID_CHARS,
        /* lpMultiByteStr */ long_path,
        /* cbMultiByte    */ USE_NULL_TERMINATION,
        /* lpWideCharStr  */ NULL,
        /* cchWideChar    */ 0 // calculate required buffer size only
    );

    if (buffer_length <= 0) {
        unsigned long err = GetLastError();
        RCLOG_WARN("string conversion failed early with error %lu: \"%s\"", err, long_path);
        free(long_path);
        return false;
    }

    size_t buffer_size = buffer_length * sizeof(WCHAR);

    WCHAR *mb_long_path = zmalloc(buffer_size);
    if (!mb_long_path) {
        RCLOG_WARN("failed to allocate %zu bytes for path string conversion", buffer_size);
        free(long_path);
        return false;
    }

    int res = MultiByteToWideChar(
        /* CodePage       */ CP_UTF8,
        /* dwFlags        */ MB_ERR_INVALID_CHARS,
        /* lpMultiByteStr */ long_path,
        /* cbMultiByte    */ USE_NULL_TERMINATION,
        /* lpWideCharStr  */ mb_long_path,
        /* cchWideChar    */ buffer_length
    );

    if (res <= 0) {
        unsigned long err = GetLastError();
        RCLOG_WARN("string conversion failed late with error %lu: \"%s\"", err, long_path);
        free(mb_long_path);
        free(long_path);
        return false;
    }

    long attributes = GetFileAttributesW(mb_long_path);

    free(mb_long_path);
    free(long_path);

    return attributes != INVALID_FILE_ATTRIBUTES;
}
