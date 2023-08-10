#ifndef XPTYPES_H
#define XPTYPES_H

#include <stdint.h>

/**
 * @file xptypes.h
 * Aliases, substitutes and checks for the X-Plane API type system.
 */

/// used for XPLMDataRef to indicate that no dataref is set/exists
#define NO_XP_DATAREF (NULL)
/// used for XPLMCommandRef to indicate that no command reference is set/exists
#define NO_XP_COMMAND (NULL)

/// the memory size (number of bytes) of a xplmType_Int
#define SIZE_XPLM_INT 4
/// the memory size (number of bytes) of a xplmType_Float
#define SIZE_XPLM_FLOAT 4
/// the memory size (number of bytes) of a xplmType_Double
#define SIZE_XPLM_DOUBLE 8

/// the identical memory size (number of bytes) needed to fit either a xplmType_Int or xplmType_Float
#define SIZE_XPLM_INT_FLOAT SIZE_XPLM_FLOAT
#if SIZE_XPLM_INT != SIZE_XPLM_FLOAT
#error size of X-Plane integer and float types do not match
#endif

/// type alias for xplmType_Int values
typedef int32_t xpint_t;
/// type alias for xplmType_Float values
typedef float xpfloat_t;
/// type alias for xplmType_Double values
typedef double xpdouble_t;

#if __SIZEOF_FLOAT__ != SIZE_XPLM_FLOAT
#error compiler/architecture uses a different size for float than X-Plane API
#endif

#if __SIZEOF_DOUBLE__ != SIZE_XPLM_DOUBLE
#error compiler/architecture uses a different size for double than X-Plane API
#endif

#endif
