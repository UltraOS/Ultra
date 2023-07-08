#pragma once

#include <common/attributes.h>

#include <stdarg.h>

enum log_level {
    LOG_LEVEL_INFO = 0,
    LOG_LEVEL_WARN = 1,
    LOG_LEVEL_ERR = 2,
    LOG_LEVEL_MAX = LOG_LEVEL_ERR,
};

void vprint(enum log_level, const char *msg, va_list vlist);

PRINTF_DECL(2, 3)
void print(enum log_level, const char *msg, ...);

#ifndef MSG_FMT
#define MSG_FMT(msg) msg
#endif

#define info(msg, ...) print(LOG_LEVEL_INFO, MSG_FMT(msg), ##__VA_ARGS__)
#define warn(msg, ...) print(LOG_LEVEL_WARN, MSG_FMT(msg)), ##__VA_ARGS__)
#define err(msg, ...)  print(LOG_LEVEL_ERR, MSG_FMT(msg)), ##__VA_ARGS__)

#define infoln(msg, ...) \
    print(LOG_LEVEL_INFO, MSG_FMT(msg) "\n", ##__VA_ARGS__)
#define warnln(msg, ...) \
    print(LOG_LEVEL_WARN, MSG_FMT(msg) "\n"), ##__VA_ARGS__)
#define errln(msg, ...)  \
    print(LOG_LEVEL_ERR, MSG_FMT(msg) "\n"), ##__VA_ARGS__)
