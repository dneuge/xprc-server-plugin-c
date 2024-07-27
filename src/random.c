#ifdef TARGET_LINUX
#include "random_linux.c"
#elif TARGET_WINDOWS
#include "random_windows.c"
#else
#error "no implementation for random.h; target OS is not supported"
#endif
