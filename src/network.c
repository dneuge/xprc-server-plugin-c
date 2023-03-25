#ifdef TARGET_LINUX
#include "network_linux.c"
#else
#error "no implementation for network.h; target OS is not supported"
#endif
