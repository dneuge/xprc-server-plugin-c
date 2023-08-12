#include <stdio.h>
#include <stdlib.h>

#include "utils.h"

int* get_random_ints(int count, int min, int max) {
    if (count < 1) {
        return NULL;
    }

    if (max < min) {
        return NULL;
    }

    // this implementation can only handle spans that fit in a single byte
    int span = max - min + 1;
    if ((span < 2) || (span > 255)) {
        return NULL;
    }

    // What follows is a stupidly naive implementation that will basically just read bytes and use them for output.
    // To make them fit the required span it just skips to the next random byte until it fits into the wanted range.
    // We want to avoid consuming too many random bytes though - instead we will accept larger random values as input
    // and modulo to the requested span. Ideally the span should be a multiple of 2 so the whole range of a byte can
    // be used but in case it's actually odd we need to figure out the largest value we can still accept for a modulo
    // operation that hopefully (this has *NOT* been mathematically proven!) should not weaken the result.
    int usable_max = span;
    while ((usable_max<<1) <= 256) {
        usable_max = usable_max << 1;
    }
    usable_max--; // last value would result in modulo 0
    //printf("get_random_ints: min=%d, max=%d, span=%d, usable_max=%d\n", min, max, span, usable_max); // DEBUG

    int *out = NULL;

    FILE *fh = fopen("/dev/random", "r");
    if (!fh) {
        goto error;
    }

    out = zalloc(sizeof(int) * count);
    if (!out) {
        goto error;
    }

    int i = 0;
    while (i < count) {
        int b = fgetc(fh);
        if (feof(fh) || ferror(fh)) {
            goto error;
        }

        if (b > usable_max) {
            //printf("get_random_ints: wasted %d\n", b); // DEBUG
            continue;
        }

        out[i++] = (b % span) + min;
    }

    fclose(fh);

    return out;

error:
    if (out) {
        free(out);
    }

    if (fh) {
        fclose(fh);
    }

    return NULL;
}
