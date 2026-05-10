#include <string.h>
#ifdef TARGET_WINDOWS
#include <windows.h>
#include <winnls.h>
#else
#include "windows_stub.h"
#endif

#include <stdlib.h>

#include "logger.h"

#include "fileio.h"
#include "fileio_internal.h"

struct file_handle_s {
    HANDLE platform_fh;
};

#define SUPPORTED_FILE_MODES (FILE_MODE_READ | FILE_MODE_WRITE)

#define USE_NULL_TERMINATION (-1)

static error_t init_file_handle(file_handle_t *fh) {
    if (!fh) {
        RCLOG_WARN("[fileio windows] init_file_handle called with NULL");
        return ERROR_UNSPECIFIC;
    }

    fh->platform_fh = INVALID_HANDLE_VALUE;

    return ERROR_NONE;
}

static WCHAR* convert_utf8_to_wchar(char *utf8) {
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
     * This file itself remains published under MIT license. If one of the API reference sources requires a more
     * restrictive license to be put into effect, the respective license shall take precedence with closely limited effect
     * in accordance to the right to sublicense MIT-licensed works. This will mainly affect binary distributions and
     * other code that might be based upon this file. To avoid licensing issues and to avoid taking over a potentially
     * wrong implementation, it is strongly recommended not to use this file for reference in other projects; follow
     * the original API docs on your own instead.
     *
     * [sdk-api] docs/sdk-api-src/content/stringapiset/nf-stringapiset-multibytetowidechar.md
     * [win32]   docs/desktop-src/Intl/code-page-identifiers.md
     */

    int buffer_length = MultiByteToWideChar(
        /* CodePage       */ CP_UTF8,
        /* dwFlags        */ MB_ERR_INVALID_CHARS,
        /* lpMultiByteStr */ utf8,
        /* cbMultiByte    */ USE_NULL_TERMINATION,
        /* lpWideCharStr  */ NULL,
        /* cchWideChar    */ 0 // calculate required buffer size only
    );

    if (buffer_length <= 0) {
        DWORD err = GetLastError();
        RCLOG_WARN("[fileio windows] string conversion failed early with error %lu: \"%s\"", err, utf8);
        return NULL;
    }

    size_t buffer_size = buffer_length * sizeof(WCHAR);

    WCHAR *out = zmalloc(buffer_size);
    if (!out) {
        RCLOG_WARN("[fileio windows] failed to allocate %zu bytes for string conversion", buffer_size);
        return NULL;
    }

    int res = MultiByteToWideChar(
        /* CodePage       */ CP_UTF8,
        /* dwFlags        */ MB_ERR_INVALID_CHARS,
        /* lpMultiByteStr */ utf8,
        /* cbMultiByte    */ USE_NULL_TERMINATION,
        /* lpWideCharStr  */ out,
        /* cchWideChar    */ buffer_length
    );

    if (res <= 0) {
        DWORD err = GetLastError();
        RCLOG_WARN("[fileio windows] string conversion failed late with error %lu: \"%s\"", err, utf8);
        free(out);
        return NULL;
    }

    return out;
}

static WCHAR* convert_utf8_to_wide_long_path(char *utf8_simple_path) {
    // we need absolute paths on Windows, otherwise file IO will fail
    size_t len = strlen(utf8_simple_path);
    bool is_absolute = (len > 3)
        && ( /* only A..Z are valid drive letters */
               (utf8_simple_path[0] >= 'A' && utf8_simple_path[0] <= 'Z')
            || (utf8_simple_path[0] >= 'a' && utf8_simple_path[0] <= 'z')
        )
        && (utf8_simple_path[1] == ':')
        && (utf8_simple_path[2] == '\\');
    if (!is_absolute) {
        RCLOG_WARN("[fileio windows] long paths can only be constructed for absolute paths; invalid: \"%s\"", utf8_simple_path);
        return NULL;
    }

    // prepend \\?\ to enable long path handling
    char *utf8_long_path = dynamic_sprintf("\\\\?\\%s", utf8_simple_path);
    if (!utf8_long_path) {
        RCLOG_WARN("[fileio windows] failed to construct long path from: \"%s\"", utf8_simple_path);
        return NULL;
    }

    WCHAR *wide_long_path = convert_utf8_to_wchar(utf8_long_path);
    if (!wide_long_path) {
        RCLOG_WARN("[fileio windows] failed to convert from UTF-8 to wide char: %s", utf8_long_path);
    }

    free(utf8_long_path);

    return wide_long_path;
}

static error_t open_file(file_handle_t *fh, file_mode_t mode, char *path) {
    /* The goal of this function is to establish compatibility with Windows operating systems.
     *
     * This function is based on API information published by Microsoft under CC-BY 4.0 and MIT licenses at:
     *
     * https://github.com/MicrosoftDocs/sdk-api/blob/9281901f3a0ecbd38f041049f4a6ade8631c240e/sdk-api-src/content/fileapi/nf-fileapi-createfilew.md
     *
     * This file itself remains published under MIT license. If one of the API reference sources requires a more
     * restrictive license to be put into effect, the respective license shall take precedence with closely limited effect
     * in accordance to the right to sublicense MIT-licensed works. This will mainly affect binary distributions and
     * other code that might be based upon this file. To avoid licensing issues and to avoid taking over a potentially
     * wrong implementation, it is strongly recommended not to use this file for reference in other projects; follow
     * the original API docs on your own instead.
     */

    error_t out_err = ERROR_NONE;

    if (!fh || mode == 0 || !path) {
        RCLOG_WARN("[fileio windows] open_file missing parameters: fh=%p, mode=0x%02x, path=%p", fh, mode, path);
        return ERROR_UNSPECIFIC;
    }

    if (fh->platform_fh != INVALID_HANDLE_VALUE) {
        RCLOG_WARN("[fileio windows] open_file called with an already open file handle %p", fh);
        return ERROR_UNSPECIFIC;
    }

    if (mode & ~SUPPORTED_FILE_MODES) {
        RCLOG_WARN("[fileio windows] open_file unsupported mode 0x%02x requested for %s", mode, path);
        return ERROR_UNSPECIFIC;
    }

    bool should_read = (mode & FILE_MODE_READ);
    bool should_write = (mode & FILE_MODE_WRITE);

    if (!(should_read || should_write)) {
        RCLOG_WARN("[fileio windows] open_file called without requesting either read or write access for %s", path);
        return ERROR_UNSPECIFIC;
    }

    DWORD access = 0;
    if (should_read) {
        access |= GENERIC_READ;
    }
    if (should_write) {
        access |= GENERIC_WRITE;
    }

    WCHAR *wide_long_path = convert_utf8_to_wide_long_path(path);
    if (!wide_long_path) {
        RCLOG_ERROR("[fileio windows] open_file failed to convert path \"%s\"", path);
        return ERROR_UNSPECIFIC;
    }

    fh->platform_fh = CreateFileW(
        /* lpFileName            */ wide_long_path,
        /* dwDesiredAccess       */ access,
        /* dwShareMode           */ should_write ? 0 : FILE_SHARE_READ,
        /* lpSecurityAttributes  */ NULL,
        /* dwCreationDisposition */ should_write ? (should_read ? OPEN_ALWAYS : CREATE_ALWAYS) : OPEN_EXISTING,
        /* dwFlagsAndAttributes  */ FILE_ATTRIBUTE_NORMAL,
        /* hTemplateFile         */ NULL
    );
    if (fh->platform_fh == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        RCLOG_WARN("[fileio windows] open_file for \"%s\" failed with platform error %lu", path, err);
        out_err = ERROR_UNSPECIFIC;
    }

    free(wide_long_path);

    return out_err;
}

static error_t read_bytes(size_t *num_read, file_handle_t *fh, char *buffer, size_t buffer_size) {
    /* The goal of this function is to establish compatibility with Windows operating systems.
     *
     * This function is based on API information published by Microsoft under CC-BY 4.0 and MIT licenses at:
     *
     * https://github.com/MicrosoftDocs/sdk-api/blob/2d7e67aa07381c0a24b869a9edb71b19cb75d14c/sdk-api-src/content/errhandlingapi/nf-errhandlingapi-setlasterror.md
     * https://github.com/MicrosoftDocs/sdk-api/blob/2d7e67aa07381c0a24b869a9edb71b19cb75d14c/sdk-api-src/content/errhandlingapi/nf-errhandlingapi-getlasterror.md
     * https://github.com/MicrosoftDocs/sdk-api/blob/81724a8ef4457859b4035358e0f9ea6d69fb270f/sdk-api-src/content/fileapi/nf-fileapi-readfile.md
     *
     * This file itself remains published under MIT license. If one of the API reference sources requires a more
     * restrictive license to be put into effect, the respective license shall take precedence with closely limited effect
     * in accordance to the right to sublicense MIT-licensed works. This will mainly affect binary distributions and
     * other code that might be based upon this file. To avoid licensing issues and to avoid taking over a potentially
     * wrong implementation, it is strongly recommended not to use this file for reference in other projects; follow
     * the original API docs on your own instead.
     */

    error_t out_err = ERROR_NONE;

    if (!num_read || !fh || !buffer) {
        RCLOG_WARN("[fileio windows] read_bytes missing parameters: num_read=%p, fh=%p, buffer=%p", num_read, fh, buffer);
        return ERROR_UNSPECIFIC;
    }

    if (buffer_size <= 0) {
        RCLOG_WARN("[fileio windows] read_bytes called with zero-length buffer");
        return ERROR_UNSPECIFIC;
    }

    if (fh->platform_fh == INVALID_HANDLE_VALUE) {
        RCLOG_WARN("[fileio windows] read_bytes called with closed file handle");
        return ERROR_UNSPECIFIC;
    }

    SetLastError(0);

    DWORD platform_num_read = 0;
    bool success = 0 != ReadFile(
        /* hFile                */ fh->platform_fh,
        /* lpBuffer             */ buffer,
        /* nNumberOfBytesToRead */ buffer_size,
        /* lpNumberOfBytesRead  */ &platform_num_read,
        /* lpOverlapped         */ NULL
    );
    *num_read = platform_num_read;
    if (!success) {
        DWORD err = GetLastError();
        RCLOG_WARN("[fileio windows] read_bytes failed (by retval) with platform error %lld", err);
        return ERROR_UNSPECIFIC;
    }

    if (*num_read < buffer_size) {
        DWORD platform_error = GetLastError();

        // Windows indicates reaching EOF as an error - but it's not an error for us
        // also, it seems that we may get no error indicated when hitting EOF the first time
        if (platform_error != 0 && platform_error != ERROR_HANDLE_EOF) {
            RCLOG_WARN("[fileio windows] read_bytes failed (by underrun) with platform error code %lld", platform_error);
            out_err = ERROR_UNSPECIFIC;
        }
    }

    return out_err;
}

static error_t write_bytes(size_t *num_written, file_handle_t *fh, char *bytes, size_t size) {
    /* The goal of this function is to establish compatibility with Windows operating systems.
     *
     * This function is based on API information published by Microsoft under CC-BY 4.0 and MIT licenses at:
     *
     * https://github.com/MicrosoftDocs/sdk-api/blob/2d7e67aa07381c0a24b869a9edb71b19cb75d14c/sdk-api-src/content/errhandlingapi/nf-errhandlingapi-setlasterror.md
     * https://github.com/MicrosoftDocs/sdk-api/blob/2d7e67aa07381c0a24b869a9edb71b19cb75d14c/sdk-api-src/content/errhandlingapi/nf-errhandlingapi-getlasterror.md
     * https://github.com/MicrosoftDocs/sdk-api/blob/81724a8ef4457859b4035358e0f9ea6d69fb270f/sdk-api-src/content/fileapi/nf-fileapi-writefile.md
     *
     * This file itself remains published under MIT license. If one of the API reference sources requires a more
     * restrictive license to be put into effect, the respective license shall take precedence with closely limited effect
     * in accordance to the right to sublicense MIT-licensed works. This will mainly affect binary distributions and
     * other code that might be based upon this file. To avoid licensing issues and to avoid taking over a potentially
     * wrong implementation, it is strongly recommended not to use this file for reference in other projects; follow
     * the original API docs on your own instead.
     */

    error_t out_err = ERROR_NONE;

    if (!num_written || !fh || !bytes) {
        RCLOG_WARN("[fileio windows] write_bytes missing parameters: num_written=%p, fh=%p, bytes=%p", num_written, fh, bytes);
        return ERROR_UNSPECIFIC;
    }

    if (size <= 0) {
        RCLOG_WARN("[fileio windows] write_bytes called with zero bytes");

        // don't actually try writing to file as the result may be undefined
        return ERROR_NONE;
    }

    SetLastError(0);

    DWORD platform_num_written = 0;
    bool success = 0 != WriteFile(
        /* hFile */ fh->platform_fh,
        /* lpBuffer */ bytes,
        /* nNumberOfBytesToWrite */ size,
        /* lpNumberOfBytesWritten */ &platform_num_written,
        /* lpOverlapped */ NULL
    );
    *num_written = platform_num_written;
    if (!success) {
        DWORD err = GetLastError();
        RCLOG_WARN("[fileio windows] write_bytes failed with platform error %lld", err);
        return ERROR_UNSPECIFIC;
    }

    if (*num_written < size) {
        DWORD platform_error = GetLastError();

        // according to documentation we may encounter ERROR_IO_PENDING which is not an actual error but just a
        // hint that the IO is being performed asynchronously
        if (platform_error != ERROR_IO_PENDING) {
            RCLOG_WARN("[fileio windows] write_bytes failed with platform error code %lld (num_written=%zu, size=%zu)", platform_error, *num_written, size);
            out_err = ERROR_INCOMPLETE;
        }
    }

    return out_err;
}

static error_t close_file(file_handle_t *fh) {
    /* The goal of this function is to establish compatibility with Windows operating systems.
     *
     * This function is based on API information published by Microsoft under CC-BY 4.0 and MIT licenses at:
     *
     * https://github.com/MicrosoftDocs/sdk-api/blob/docs/sdk-api-src/content/handleapi/nf-handleapi-closehandle.md
     *   retrieved 887907145c7e22dbbfdebe4f20cdaecc123e9bea from 19 Nov 2020 on 9 May 2026
     *
     * This file itself remains published under MIT license. If one of the API reference sources requires a more
     * restrictive license to be put into effect, the respective license shall take precedence with closely limited effect
     * in accordance to the right to sublicense MIT-licensed works. This will mainly affect binary distributions and
     * other code that might be based upon this file. To avoid licensing issues and to avoid taking over a potentially
     * wrong implementation, it is strongly recommended not to use this file for reference in other projects; follow
     * the original API docs on your own instead.
     */

    error_t out_err = ERROR_NONE;

    if (!fh) {
        RCLOG_WARN("[fileio windows] close_file called with NULL");
        return ERROR_UNSPECIFIC;
    }

    if (fh->platform_fh == INVALID_HANDLE_VALUE) {
        RCLOG_WARN("[fileio windows] close_file called with an already closed file handle");
        return ERROR_NONE;
    }

    if (CloseHandle(fh->platform_fh) == 0) {
        // documentation doesn't say what happens to the handle if closing fails, assume it is being kept open (don't invalidate)
        DWORD err = GetLastError();
        RCLOG_WARN("[fileio windows] closing file failed with platform error code %lu", err);
        return ERROR_INCOMPLETE;
    }

    fh->platform_fh = INVALID_HANDLE_VALUE;

    return out_err;
}

static bool is_open_file(file_handle_t *fh) {
    if (!fh) {
        RCLOG_WARN("[fileio windows] is_open_file called with NULL");
        return false;
    }

    return fh->platform_fh != INVALID_HANDLE_VALUE;
}

static bool check_eof(file_handle_t *fh) {
    /* The goal of this function is to establish compatibility with Windows operating systems.
     *
     * This function is based on API information published by Microsoft under CC-BY 4.0 and MIT licenses at:
     *
     * https://github.com/MicrosoftDocs/win32/blob/7288a6d91125f7f28690e8db087d1f7c80ce8c3f/desktop-src/FileIO/testing-for-the-end-of-a-file.md
     * https://github.com/MicrosoftDocs/sdk-api/blob/2d7e67aa07381c0a24b869a9edb71b19cb75d14c/sdk-api-src/content/errhandlingapi/nf-errhandlingapi-setlasterror.md
     * https://github.com/MicrosoftDocs/sdk-api/blob/2d7e67aa07381c0a24b869a9edb71b19cb75d14c/sdk-api-src/content/errhandlingapi/nf-errhandlingapi-getlasterror.md
     * https://github.com/MicrosoftDocs/sdk-api/blob/81724a8ef4457859b4035358e0f9ea6d69fb270f/sdk-api-src/content/fileapi/nf-fileapi-readfile.md
     * https://github.com/MicrosoftDocs/sdk-api/blob/81724a8ef4457859b4035358e0f9ea6d69fb270f/sdk-api-src/content/fileapi/nf-fileapi-setfilepointer.md
     *
     * This file itself remains published under MIT license. If one of the API reference sources requires a more
     * restrictive license to be put into effect, the respective license shall take precedence with closely limited effect
     * in accordance to the right to sublicense MIT-licensed works. This will mainly affect binary distributions and
     * other code that might be based upon this file. To avoid licensing issues and to avoid taking over a potentially
     * wrong implementation, it is strongly recommended not to use this file for reference in other projects; follow
     * the original API docs on your own instead.
     */

    if (!fh) {
        RCLOG_WARN("[fileio windows] check_eof called with NULL");
        return true;
    }

    if (fh->platform_fh == INVALID_HANDLE_VALUE) {
        RCLOG_WARN("[fileio windows] check_eof called with closed file handle");
        return true;
    }

    // according to the documentation we can only figure out if we are at EOF by reading from the file and
    // checking the error code

    // clear any existing error code to be able to detect the error of the operation being carried out
    SetLastError(0);

    // try to read one byte
    char buffer[1] = {0};
    DWORD num_read = 0;
    bool success = 0 != ReadFile(
        /* hFile                */ fh->platform_fh,
        /* lpBuffer             */ buffer,
        /* nNumberOfBytesToRead */ 1,
        /* lpNumberOfBytesRead  */ &num_read,
        /* lpOverlapped         */ NULL
    );
    DWORD read_error = GetLastError();

    // according to the documentation trying to read past EOF is indicated as success; if it fails we have a true error
    if (!success) {
        DWORD err = GetLastError();
        RCLOG_WARN("[fileio windows] check_eof failed with platform error %lld", err);
        return true;
    }

    if (num_read < 0) {
        RCLOG_ERROR("[fileio windows] check_eof reading moved file cursor backwards? got num_read=%lld without error, closing file", num_read);
        close_file(fh);
        return true;
    }

    // we were at EOF if we read 0 bytes and have an EOF error
    bool was_eof = (num_read == 0) || (read_error == ERROR_HANDLE_EOF);
    RCLOG_TRACE("[fileio windows] num_read=%lld, read_error=%lld => was_eof=%d", num_read, read_error, was_eof);

    // unfortunately, if we were not at EOF yet, we just forwarded the cursor, so we have to seek back to the original
    // file position
    if (num_read != 0) {
        success = 0 != SetFilePointer(
            /* hFile            */ fh->platform_fh,
            /* lDistanceToMove  */ -num_read,
            /* lpNewFilePointer */ NULL,
            /* dwMoveMethod     */ FILE_CURRENT
        );
        if (!success) {
            DWORD err = GetLastError();
            RCLOG_WARN("[fileio windows] check_eof corrupted file pointer on backtracking, closing file; platform error %lld", err);
            close_file(fh);
            return true;
        }
    }

    return was_eof;
}

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
     */

    if (!path) {
        RCLOG_ERROR("[fileio windows] check_file_exists called without path; unpredictable behaviour (indicating file would not exist)");
        return false;
    }

    WCHAR *wide_long_path = convert_utf8_to_wide_long_path(path);
    if (!wide_long_path) {
        RCLOG_ERROR("[fileio windows] check_file_exists failed to convert path \"%s\"", path);
        return false;
    }

    ULONG attributes = GetFileAttributesW(wide_long_path);

    free(wide_long_path);

    return attributes != INVALID_FILE_ATTRIBUTES;
}

error_t ensure_directory_exists(char *path) {
    /* The goal of this function is to establish compatibility with Windows operating systems.
     *
     * This function is based on API information published by Microsoft under CC-BY 4.0 and MIT licenses at:
     *
     * https://github.com/MicrosoftDocs/sdk-api/blob/12ea658d1d81468de8b2c521ed1e667942767c51/sdk-api-src/content/fileapi/nf-fileapi-createdirectoryw.md
     * https://github.com/MicrosoftDocs/win32/blob/2ec0df659644a793ed4f6160f238a95c9d9a9dcf/desktop-src/FileIO/file-attribute-constants.md
     *
     * This file itself remains published under MIT license. If one of the API reference sources requires a more
     * restrictive license to be put into effect, the respective license shall take precedence with closely limited effect
     * in accordance to the right to sublicense MIT-licensed works. This will mainly affect binary distributions and
     * other code that might be based upon this file. To avoid licensing issues and to avoid taking over a potentially
     * wrong implementation, it is strongly recommended not to use this file for reference in other projects; follow
     * the original API docs on your own instead.
     */

    error_t out_err = ERROR_NONE;

    if (!path) {
        RCLOG_ERROR("[fileio windows] ensure_directory_exists called without path");
        return ERROR_UNSPECIFIC;
    }

    WCHAR *wide_long_path = convert_utf8_to_wide_long_path(path);
    if (!wide_long_path) {
        RCLOG_ERROR("[fileio windows] ensure_directory_exists failed to convert path \"%s\"", path);
        return ERROR_UNSPECIFIC;
    }

    // do not attempt creation if the directory already exists
    // we check for *any* existence; we also continue if the path is actually a file or symlink
    ULONG attributes = GetFileAttributesW(wide_long_path);
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        RCLOG_DEBUG("[fileio windows] directory already exist, not creating: %s", path);
        goto end;
    }

    // try creating the directory
    RCLOG_DEBUG("[fileio windows] directory does not exist, creating: %s", path);
    bool success = 0 != CreateDirectoryW(
        /* lpPathName           */ wide_long_path,
        /* lpSecurityAttributes */ NULL
    );
    if (!success) {
        DWORD err = GetLastError();
        RCLOG_WARN("[fileio windows] CreateDirectoryW failed with platform error %lld", err);
        out_err = ERROR_UNSPECIFIC;
        goto end;
    }

    // verify that it really exists now
    // note that this time we check can that what we just created ourselves is an actual directory because we know what
    // it should be
    attributes = GetFileAttributesW(wide_long_path);
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
        goto end;
    }

    RCLOG_WARN("[fileio windows] created path is not a directory, attributes=0x%lx: %s", attributes, path);
    out_err = ERROR_INCOMPLETE;

end:
    free(wide_long_path);

    return out_err;
}