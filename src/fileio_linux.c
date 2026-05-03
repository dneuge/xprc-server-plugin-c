#include <errno.h>
#include <stdbool.h>
#include <unistd.h>

#include "logger.h"

#include "fileio.h"

bool check_file_exists(char *path) {
    if (!path) {
        RCLOG_ERROR("[fileio] check_file_exists called without path; unpredictable behaviour (indicating file would not exist)");
        return false;
    }

    if (access(path, F_OK) == 0) {
        return true;
    }

    return (errno != ENOENT);
}
