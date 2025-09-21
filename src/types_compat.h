#ifndef TYPES_COMPAT_H
#define TYPES_COMPAT_H

#ifndef HAVE_SSIZE_T
#include <stdint.h>
typedef int64_t ssize_t;
#endif

#endif
