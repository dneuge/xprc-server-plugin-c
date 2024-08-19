#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logger.h"
#include "password.h"

/* Repeatedly calls generate_password() and prints the resulting strings for manual testing.
 *
 * The wanted number of iterations can be provided as a command-line argument.
 */

int main(int argc, char **argv) {
    int num_iterations = 10;
    if (argc > 1) {
        num_iterations = atoi(argv[1]);
    }

    xprc_log_init();
    xprc_set_min_log_level_console(RCLOG_LEVEL_TRACE);

    unsigned long count[256] = {0,};

    for (int i=0; i<num_iterations; i++) {
        char *pwd = generate_password();
        if (!pwd) {
            RCLOG_ERROR("generated password is NULL");
            return 1;
        }

        RCLOG_INFO("generated: %s", pwd);

        // increase the count for that character
        for (int j=0; j<strlen(pwd); j++) {
            unsigned char ch = ((unsigned char*) pwd)[j];
            count[ch]++;
        }

        free(pwd);
    }

    RCLOG_INFO("Count Char  Expected");
    for (int i=0; i<256; i++) {
        unsigned long occurrences = count[i];
        unsigned char ch = i;
        bool is_printable = (ch >= 0x20 && ch <= 0x7E);
        bool is_expected = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || (ch == '.') || (ch == '-');

        bool should_list = is_expected || (occurrences > 0);
        if (!should_list) {
            continue;
        }

        if (is_printable) {
            RCLOG_INFO("%5ld %03d %c %s", occurrences, ch, ch, is_expected ? "OK" : "Unexpected");
        } else {
            RCLOG_INFO("%5ld %03d   %s", occurrences, ch, is_expected ? "OK" : "Unexpected");
        }
    }

    xprc_log_destroy();

    return 0;
}