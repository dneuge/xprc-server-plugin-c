#include <stdlib.h>
#include <string.h>

#include "utils.h"

char* copy_partial_string(char *s, int length) {
    char *copy = malloc(length + 1);
    if (!copy) {
        return NULL;
    }
    
    memcpy(copy, s, length);
    copy[length] = 0;
    
    return copy;
}

char* copy_string(char *s) {
    if (!s) {
        return NULL;
    }

    return copy_partial_string(s, strlen(s));
}
