/* The goal of this file is to establish compatibility with Windows operating systems.
 *
 * This file is based on API information published by Microsoft under CC-BY 4.0 and MIT licenses at:
 *
 *   https://github.com/MicrosoftDocs/sdk-api
 *   revision 5da3012685fee3b1dbbefe7fa1f9a9935b9fa14e (2 Aug 2024)
 *
 *   https://github.com/MicrosoftDocs/win32
 *   revision 7d616e305727028f71d325dd5c411c0f04c964de (2 Aug 2024)
 *
 *   see repositories at specified revisions for detailed license information
 *
 * Official API documentation omits headers and thus low-level type information. Missing information has
 * been substituted in reference to headers distributed as part of wine which are published under terms of
 * LGPL 2.1:
 *
 *   https://github.com/wine-mirror/wine/blob/master/include/
 *
 * This file itself remains published under MIT license. If one of the API reference sources requires a more
 * restrictive license to be put into effect, the respective license shall take precedence with closely limited effect
 * in accordance to the right to sublicense MIT-licensed works. This will mainly affect binary distributions and
 * other code that might be based upon this file. To avoid licensing issues and to avoid taking over a potentially
 * wrong implementation, it is strongly recommended not to use this file for reference in other projects; follow
 * the original API docs on your own instead.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

// Windows API
#include <windows.h>
#include <ntstatus.h>
#include <bcrypt.h>

#include "random.h"

#include "logger.h"
#include "utils.h"

int* get_random_ints(int count, int min, int max) {
    // Microsoft API docs:
    // [sdk-api] docs/sdk-api-src/content/wincrypt/nf-wincrypt-cryptgenrandom.md
    // [win32]   docs/desktop-src/SecCNG/systemprng.md
    // [sdk-api] docs/sdk-api-src/content/bcrypt/nf-bcrypt-bcryptgenrandom.md

    unsigned char buffer[] = {0};

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
    //RCLOG_TRACE("get_random_ints: min=%d, max=%d, span=%d, usable_max=%d", min, max, span, usable_max); // DEBUG: only for development, does not change at runtime

    int *out = zmalloc(sizeof(int) * count);
    if (!out) {
        return NULL;
    }

    int i = 0;
    while (i < count) {
        NTSTATUS status = BCryptGenRandom(
            /* hAlgorithm */ NULL, // use system preferred RNG
            /* pbBuffer   */ buffer,
            /* cbBuffer   */ 1,
            /* dwFlags    */ BCRYPT_USE_SYSTEM_PREFERRED_RNG
        );

        if (status != STATUS_SUCCESS) {
            RCLOG_WARN("get_random_ints failed to call BCryptGenRandom: %ld", status);
            free(out);
            return NULL;
        }

        int b = buffer[0];

        if (b > usable_max) {
            //RCLOG_TRACE("get_random_ints: wasted %d", b); // DEBUG: only for development, does not change at runtime
            continue;
        }

        out[i++] = (b % span) + min;
    }

    return out;
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
