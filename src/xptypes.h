#ifndef XPTYPES_H
#define XPTYPES_H

#include <stdint.h>

#define NO_XP_DATAREF (NULL)
#define NO_XP_COMMAND (NULL)

#define SIZE_XPLM_INT 4
#define SIZE_XPLM_FLOAT 4
#define SIZE_XPLM_DOUBLE 8

#define SIZE_XPLM_INT_FLOAT SIZE_XPLM_FLOAT
#if SIZE_XPLM_INT != SIZE_XPLM_FLOAT
#error size of X-Plane integer and float types do not match
#endif

typedef int32_t xpint_t;
typedef float xpfloat_t;
typedef double xpdouble_t;

#if __SIZEOF_FLOAT__ != SIZE_XPLM_FLOAT
#error compiler/architecture uses a different size for float than X-Plane API
#endif

#if __SIZEOF_DOUBLE__ != SIZE_XPLM_DOUBLE
#error compiler/architecture uses a different size for double than X-Plane API
#endif

#endif
