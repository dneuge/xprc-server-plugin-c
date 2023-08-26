#ifndef XPRC_PASSWORD_H
#define XPRC_PASSWORD_H

/**
 * @file password.h password generation and validation
 */

#include <stdbool.h>

/**
 * Generates a random password for XPRC.
 * @return random password; NULL on error
 */
char* generate_password();

/**
 * Checks if the given strings fulfills the password policy.
 * @param password password to validate
 * @return true if password fulfills policy, false if not
 */
bool validate_password(char *password);

#endif
