#include "command_drls_unspecific.h"

#if COMMAND_DRLS_UNSPECIFIC_SUPPORTED
#include "command_drls_unspecific_impl.c"
#else
#include "command_drls_unspecific_dummy.c"
#endif
