#include <stdlib.h>
#include <string.h>

#include "random.h"
#include "utils.h"

#include "password.h"

#define PASSWORD_LENGTH 32

// the number of characters should be a multiple of 2; see random_linux.c implementation
static const char password_chars[] = "abcdefghijklmnopqrstuvwxyz"
                                     "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                     "0123456789"
                                     "-.";

char* generate_password() {
    char *out = zalloc(PASSWORD_LENGTH + 1);
    if (!out) {
        return NULL;
    }

    int *random = get_random_ints(PASSWORD_LENGTH, 0, strlen(password_chars)-1);
    if (!random) {
        free(out);
        return NULL;
    }

    for (int i=0; i<PASSWORD_LENGTH; i++) {
        out[i] = password_chars[random[i]];
    }

    free(random);

    return out;
}
