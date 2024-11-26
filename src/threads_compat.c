#ifdef NEED_C11_THREADS_WRAPPER

#ifdef TARGET_MACOS

#include "threads_macos.c"

#elif TARGET_WINDOWS

/* Mesa C11 needs to be linked separately */

#else
#error "Missing C11 threads compatibility wrapper for target OS!"
#endif

#endif
