#include <stdlib.h>
#include <time.h>

#include "random.h"

int* get_random_ints(int count, int min, int max) {
    // FIXME: implement for Windows
    return NULL;
}

long get_random_long_insecure() {
    long res = 0;

    // NOTE: sizeof(int) is unreliable in the way that it over-reports the actual length on x86 to the size of a long,
    //       probably because it is handled as a long internally. Instead of "concatenating" ints we take the lowest
    //       byte of a random and construct a long byte-by-byte, just to make sure this has really the intended effect
    //       regardless of compiler/architecture specifics.
    for (int i=0; i<sizeof(long); i++) {
        res = (res << 8) ^ (rand() & 0xFF);
    }

    return res;
}

void initialize_insecure_random() {
    srand(time(NULL));
}
