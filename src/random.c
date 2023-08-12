#ifdef TARGET_LINUX
#include "random_linux.c"
#else
#error "no implementation for random.h; target OS is not supported"
#endif
