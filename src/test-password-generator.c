#include <stdio.h>
#include <stdlib.h>

#include "password_generator.h"

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