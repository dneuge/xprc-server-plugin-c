#if defined(TARGET_LINUX) || defined(TARGET_MACOS)
// FIXME: verify this actually works on MacOS an does not just compile
#include "random_linux.c"
#elif TARGET_WINDOWS
#include "random_windows.c"
#else
#error "no implementation for random.h; target OS is not supported"
#endif
