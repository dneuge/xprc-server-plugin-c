#ifndef XPRC_RANDOM_H
#define XPRC_RANDOM_H

/**
 * @file random.h random number utilities
 */

/**
 * Generates the specified number of random integers from a reasonably good source of entropy (not crypto-secure but
 * attempting best effort to get somewhat better than default random numbers, do not use for anything that needs
 * guaranteed secure randomness).
 * @param count number of random numbers to generate
 * @param min minimum value (inclusive)
 * @param max maximum value (inclusive)
 * @return an array of random integers; NULL on error
 */
int* get_random_ints(int count, int min, int max);

/**
 * Generates a random long integer from the default insecure random generator which must have been seeded before.
 * @return a single long integer generated from default insecure random generator
 */
long get_random_long_insecure();

/**
 * Initializes the default insecure random generator using any seed.
 */
void initialize_insecure_random();

#endif //XPRC_RANDOM_H
