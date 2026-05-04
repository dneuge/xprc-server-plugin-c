#ifndef XPRC_TARGET_H
#define XPRC_TARGET_H

// FIXME: detect in CMake based on actual type sizes
#ifdef TARGET_LINUX
#define INT_SIZE 4
#else
#define INT_SIZE 2
#endif

#if INT_SIZE==4
#define INT64_FORMAT "%ld"
#define UINT64_FORMAT "%lu"
#elif INT_SIZE==2
#define INT64_FORMAT "%lld"
#define UINT64_FORMAT "%llu"
#else
#error "unhandled integer size"
#endif

#endif //XPRC_TARGET_H