#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

#include "logger.h"

#include "fileio.h"
#include "fileio_internal.h"

// FIXME: for Linux translate paths from UTF-8 to native system encoding, alternatively verify system uses UTF-8 (macOS should always be using UTF-8)

struct file_handle_s {
    FILE *platform_fh;
};

#define SUPPORTED_FILE_MODES (FILE_MODE_READ | FILE_MODE_WRITE)

static error_t init_file_handle(file_handle_t *fh) {
    if (!fh) {
        RCLOG_WARN("[fileio standard] init_file_handle called with NULL");
        return ERROR_UNSPECIFIC;
    }

    fh->platform_fh = NULL;

    return ERROR_NONE;
}

static error_t open_file(file_handle_t *fh, file_mode_t mode, char *path) {
    if (!fh || mode == 0 || !path) {
        RCLOG_WARN("[fileio standard] open_file missing parameters: fh=%p, mode=0x%02x, path=%p", fh, mode, path);
        return ERROR_UNSPECIFIC;
    }

    if (fh->platform_fh) {
        RCLOG_WARN("[fileio standard] open_file called with an already open file handle %p => %p", fh, fh->platform_fh);
        return ERROR_UNSPECIFIC;
    }

    if (mode & ~SUPPORTED_FILE_MODES) {
        RCLOG_WARN("[fileio standard] open_file unsupported mode 0x%02x requested for %s", mode, path);
        return ERROR_UNSPECIFIC;
    }

    bool should_read = (mode & FILE_MODE_READ);
    bool should_write = (mode & FILE_MODE_WRITE);

    if (should_read && should_write) {
        RCLOG_WARN("[fileio standard] open_file does not support reading and writing at same time; requested for %s", path);
        return ERROR_UNSPECIFIC;
    }

    if (!(should_read || should_write)) {
        RCLOG_WARN("[fileio standard] open_file called without requesting either read or write access for %s", path);
        return ERROR_UNSPECIFIC;
    }

    fh->platform_fh = fopen(path, should_write ? "wb" : "rb");
    if (!fh->platform_fh) {
        RCLOG_WARN("[fileio standard] failed to open %s for %s access", path, should_write ? "write" : "read");
        return ERROR_UNSPECIFIC;
    }

    return ERROR_NONE;
}

static error_t read_bytes(size_t *num_read, file_handle_t *fh, char *buffer, size_t buffer_size) {
    error_t out_err = ERROR_NONE;

    if (!num_read || !fh || !buffer) {
        RCLOG_WARN("[fileio standard] read_bytes missing parameters: num_read=%p, fh=%p, buffer=%p", num_read, fh, buffer);
        return ERROR_UNSPECIFIC;
    }

    if (buffer_size <= 0) {
        RCLOG_WARN("[fileio standard] read_bytes called with zero-length buffer");
        return ERROR_UNSPECIFIC;
    }

    if (!fh->platform_fh) {
        RCLOG_WARN("[fileio standard] read_bytes called with closed file handle");
        return ERROR_UNSPECIFIC;
    }

    *num_read = fread(buffer, 1, buffer_size, fh->platform_fh);
    if (*num_read < buffer_size) {
        int platform_error = ferror(fh->platform_fh);
        if (platform_error != 0) {
            RCLOG_WARN("[fileio standard] read_bytes failed with platform error code %d", platform_error);
            out_err = ERROR_UNSPECIFIC;
        }
    }

    return out_err;
}

static error_t write_bytes(size_t *num_written, file_handle_t *fh, char *bytes, size_t size) {
    error_t out_err = ERROR_NONE;

    if (!num_written || !fh || !bytes) {
        RCLOG_WARN("[fileio standard] write_bytes missing parameters: num_written=%p, fh=%p, bytes=%p", num_written, fh, bytes);
        return ERROR_UNSPECIFIC;
    }

    if (size <= 0) {
        RCLOG_WARN("[fileio standard] write_bytes called with zero bytes");

        // don't actually try writing to file as the result may be undefined
        return ERROR_NONE;
    }

    *num_written = fwrite(bytes, 1, size, fh->platform_fh);
    if (*num_written < size) {
        int platform_error = ferror(fh->platform_fh);
        if (platform_error != 0) {
            RCLOG_WARN("[fileio standard] write_bytes failed with platform error code %d", platform_error);
            out_err = ERROR_INCOMPLETE;
        }
    }

    return out_err;
}

static error_t close_file(file_handle_t *fh) {
    error_t out_err = ERROR_NONE;

    if (!fh) {
        RCLOG_WARN("[fileio standard] close_file called with NULL");
        return ERROR_UNSPECIFIC;
    }

    if (!fh->platform_fh) {
        RCLOG_WARN("[fileio standard] close_file called with an already closed file handle");
        return ERROR_NONE;
    }

    int res = fclose(fh->platform_fh);
    if (res != 0) {
        // Linux man-page says we should treat the file as closed anyway
        RCLOG_WARN("[fileio standard] closing file failed with platform error code %d", res);
        out_err = ERROR_INCOMPLETE;
    }

    fh->platform_fh = NULL;

    return out_err;
}

static bool is_open_file(file_handle_t *fh) {
    if (!fh) {
        RCLOG_WARN("[fileio standard] is_open_file called with NULL");
        return false;
    }

    return fh->platform_fh != NULL;
}

static bool check_eof(file_handle_t *fh) {
    if (!fh) {
        RCLOG_WARN("[fileio standard] check_eof called with NULL");
        return true;
    }

    if (!fh->platform_fh) {
        RCLOG_WARN("[fileio standard] check_eof called with closed file handle");
        return true;
    }

    return feof(fh->platform_fh) != 0;
}

bool check_file_exists(char *path) {
    if (!path) {
        RCLOG_ERROR("[fileio standard] check_file_exists called without path; unpredictable behaviour (indicating file would not exist)");
        return false;
    }

    if (access(path, F_OK) == 0) {
        return true;
    }

    return (errno != ENOENT);
}

error_t ensure_directory_exists(char *path) {
    if (!path) {
        RCLOG_ERROR("[fileio standard] ensure_directory_exists called without path");
        return ERROR_UNSPECIFIC;
    }

    // do not attempt creation if the directory already exists
    if (check_file_exists(path)) {
        RCLOG_DEBUG("[fileio standard] directory already exist, not creating: %s", path);
        return ERROR_NONE;
    }

    // try creating the directory
    RCLOG_DEBUG("[fileio standard] directory does not exist, creating: %s", path);
    int res = mkdir(path, 0700);
    if (res == -1) {
        RCLOG_WARN("[fileio standard] mkdir failed, errno=%d", errno);
        return ERROR_UNSPECIFIC;
    } else if (res != 0) {
        RCLOG_WARN("[fileio standard] mkdir failed with unknown result code %d", res);
        return ERROR_UNSPECIFIC;
    }

    // verify that it really exists now
    if (check_file_exists(path)) {
        return ERROR_NONE;
    }

    RCLOG_WARN("[fileio standard] directory does not exist after error-free attempt to create it: %s", path);

    return ERROR_UNSPECIFIC;
}
