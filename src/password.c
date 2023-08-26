#include <stdlib.h>
#include <string.h>

#include "random.h"
#include "utils.h"

#include "password.h"

#define PASSWORD_LENGTH 32

#define MIN_PASSWORD_LENGTH 15
#define MAX_PASSWORD_LENGTH 1000 /* password must fit into network line buffer */
#define MIN_PASSWORD_UPPER_CASE 3
#define MIN_PASSWORD_LOWER_CASE 3
#define MIN_PASSWORD_NUMBERS 3
#define MIN_PASSWORD_SPECIAL 3

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

    int max_random_index = (int) strlen(password_chars) - 1;
    while (!validate_password(out)) {
        int *random = get_random_ints(PASSWORD_LENGTH, 0, max_random_index);
        if (!random) {
            free(out);
            return NULL;
        }

        for (int i = 0; i < PASSWORD_LENGTH; i++) {
            out[i] = password_chars[random[i]];
        }

        free(random);
    }

    return out;
}

bool validate_password(char *password) {
    if (!password) {
        return false;
    }

    size_t length = strlen(password);
    if ((length < MIN_PASSWORD_LENGTH) || (length > MAX_PASSWORD_LENGTH)) {
        return false;
    }

    int num_lower_case = 0;
    int num_upper_case = 0;
    int num_numbers = 0;
    int num_special = 0;

    for (size_t i=0; i<length; i++) {
        unsigned char ch = ((unsigned char *) password)[i];

        if ((ch >= 'a') && (ch <= 'z')) {
            num_lower_case++;
        } else if ((ch >= 'A') && (ch <= 'Z')) {
            num_upper_case++;
        } else if ((ch >= '0') && (ch <= '9')) {
            num_numbers++;
        } else if ((ch == '\r') || (ch == '\n')) {
            // reserved characters which cannot be used for network communication
            return false;
        } else {
            num_special++;
        }
    }

    bool is_valid = (num_lower_case >= MIN_PASSWORD_LOWER_CASE)
                    && (num_upper_case >= MIN_PASSWORD_UPPER_CASE)
                    && (num_numbers >= MIN_PASSWORD_NUMBERS)
                    && (num_special >= MIN_PASSWORD_SPECIAL);

    return is_valid;
}
