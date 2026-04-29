#ifndef XPRC_LOGGER_H
#define XPRC_LOGGER_H

#include <stdbool.h>
#include <stdint.h>

#define RCLOG_LEVEL_ERROR 127
#define RCLOG_LEVEL_WARN  63
#define RCLOG_LEVEL_INFO  31
#define RCLOG_LEVEL_DEBUG 15
#define RCLOG_LEVEL_TRACE 7

#ifndef RCLOG_COMPILED_MIN_LOG_LEVEL
#define RCLOG_COMPILED_MIN_LOG_LEVEL RCLOG_LEVEL_INFO
#endif

#if (RCLOG_COMPILED_MIN_LOG_LEVEL <= RCLOG_LEVEL_ERROR)
#define RCLOG_ERROR(format, ...) xprc_log(RCLOG_LEVEL_ERROR, format __VA_OPT__(,) __VA_ARGS__)
#else
#define RCLOG_ERROR(format, ...)
#endif

#if (RCLOG_COMPILED_MIN_LOG_LEVEL <= RCLOG_LEVEL_WARN)
#define RCLOG_WARN(format, ...) xprc_log(RCLOG_LEVEL_WARN, format __VA_OPT__(,) __VA_ARGS__)
#else
#define RCLOG_WARN(format, ...)
#endif

#if (RCLOG_COMPILED_MIN_LOG_LEVEL <= RCLOG_LEVEL_INFO)
#define RCLOG_INFO(format, ...) xprc_log(RCLOG_LEVEL_INFO, format __VA_OPT__(,) __VA_ARGS__)
#else
#define RCLOG_INFO(format, ...)
#endif

#if (RCLOG_COMPILED_MIN_LOG_LEVEL <= RCLOG_LEVEL_DEBUG)
#define RCLOG_DEBUG(format, ...) xprc_log(RCLOG_LEVEL_DEBUG, format __VA_OPT__(,) __VA_ARGS__)
#else
#define RCLOG_DEBUG(format, ...)
#endif

#if (RCLOG_COMPILED_MIN_LOG_LEVEL <= RCLOG_LEVEL_TRACE)
#define RCLOG_TRACE(format, ...) xprc_log(RCLOG_LEVEL_TRACE, format __VA_OPT__(,) __VA_ARGS__)
#else
#define RCLOG_TRACE(format, ...)
#endif

// FIXME: detect in CMake and provide a define specific for size/type of timestamps instead
#ifdef TARGET_LINUX
#define INT64_FORMAT "%ld"
#define UINT64_FORMAT "%lu"
#else
#define INT64_FORMAT "%lld"
#define UINT64_FORMAT "%llu"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t xprc_log_level_t;

void xprc_log_init();
void xprc_log_destroy();

__attribute__((__format__ (__printf__, 2, 3)))
void xprc_log(xprc_log_level_t level, const char *format, ...);

void xprc_set_min_log_level_console(xprc_log_level_t level);
void xprc_set_min_log_level_xplane(xprc_log_level_t level);

bool xprc_is_log_level_enabled(xprc_log_level_t level);

#if (RCLOG_COMPILED_MIN_LOG_LEVEL <= RCLOG_LEVEL_TRACE)
#define RCLOG_IS_TRACE_ENABLED() xprc_is_log_level_enabled(RCLOG_LEVEL_TRACE)
#else
#define RCLOG_IS_TRACE_ENABLED() (false)
#endif

#ifdef __cplusplus
}
#endif

#endif
