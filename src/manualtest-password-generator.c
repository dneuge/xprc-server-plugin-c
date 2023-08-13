#include <stdio.h>
#include <stdlib.h>

#include "password_generator.h"

/* Repeatedly calls generate_password() and prints the resulting strings for manual testing.
 *
 * The wanted number of iterations can be provided as a command-line argument.
 */

int main(int argc, char **argv) {
    int num_iterations = 10;
    if (argc > 1) {
        num_iterations = atoi(argv[1]);
    }

    for (int i=0; i<num_iterations; i++) {
        char *pwd = generate_password();
        printf("%s\n", pwd);
        free(pwd);
    }

    return 0;
}