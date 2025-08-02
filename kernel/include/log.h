#pragma once

#include <common/attributes.h>
#include <common/helpers.h>

#include <stdarg.h>

/*
 * Copy log levels from the syslog standard since we have to expose a /dev/kmsg
 * anyway, might as well keep our native implementation compatible.
 */
#define SYSLOG_EMERG   0
#define SYSLOG_ALERT   1
#define SYSLOG_CRIT    2
#define SYSLOG_ERR     3
#define SYSLOG_WARNING 4
#define SYSLOG_NOTICE  5
#define SYSLOG_INFO    6
#define SYSLOG_DEBUG   7

enum log_level {
    LOG_LEVEL_EMERG   = SYSLOG_EMERG,
    LOG_LEVEL_ALERT   = SYSLOG_ALERT,
    LOG_LEVEL_CRIT    = SYSLOG_CRIT,
    LOG_LEVEL_ERR     = SYSLOG_ERR,
    LOG_LEVEL_WARN    = SYSLOG_WARNING,
    LOG_LEVEL_NOTICE  = SYSLOG_NOTICE,
    LOG_LEVEL_INFO    = SYSLOG_INFO,
    LOG_LEVEL_DEBUG   = SYSLOG_DEBUG,
    LOG_LEVEL_COUNT,
    LOG_LEVEL_DEFAULT = LOG_LEVEL_NOTICE,
};

// Ascii SOH (Start Of Heading)
#define LOG_LEVEL_PREFIX_CHAR '\x01'
#define LOG_LEVEL_PREFIX "\x01"

#define LOG_EMERG   LOG_LEVEL_PREFIX TO_STR(SYSLOG_EMERG)
#define LOG_ALERT   LOG_LEVEL_PREFIX TO_STR(SYSLOG_ALERT)
#define LOG_CRIT    LOG_LEVEL_PREFIX TO_STR(SYSLOG_CRIT)
#define LOG_ERR     LOG_LEVEL_PREFIX TO_STR(SYSLOG_ERR)
#define LOG_WARN    LOG_LEVEL_PREFIX TO_STR(SYSLOG_WARNING)
#define LOG_NOTICE  LOG_LEVEL_PREFIX TO_STR(SYSLOG_NOTICE)
#define LOG_INFO    LOG_LEVEL_PREFIX TO_STR(SYSLOG_INFO)
#define LOG_DEBUG   LOG_LEVEL_PREFIX TO_STR(SYSLOG_DEBUG)

void vprint(const char *msg, va_list vlist);

PRINTF_DECL(1, 2)
void print(const char *msg, ...);

#ifndef MSG_FMT
#define MSG_FMT(msg) msg
#endif

#define pr_emerg(msg, ...)   print(LOG_EMERG   MSG_FMT(msg), ##__VA_ARGS__)
#define pr_alert(msg, ...)   print(LOG_ALERT   MSG_FMT(msg), ##__VA_ARGS__)
#define pr_crit(msg, ...)    print(LOG_CRIT    MSG_FMT(msg), ##__VA_ARGS__)
#define pr_err(msg, ...)     print(LOG_ERR     MSG_FMT(msg), ##__VA_ARGS__)
#define pr_warn(msg, ...)    print(LOG_WARN    MSG_FMT(msg), ##__VA_ARGS__)
#define pr_notice(msg, ...)  print(LOG_NOTICE  MSG_FMT(msg), ##__VA_ARGS__)
#define pr_info(msg, ...)    print(LOG_INFO    MSG_FMT(msg), ##__VA_ARGS__)
#define pr_debug(msg, ...)   print(LOG_DEBUG   MSG_FMT(msg), ##__VA_ARGS__)

// Defined in arch/registers.h
struct registers;

/*
 * Dump the current stack trace or the stack trace of the register state
 * specified in registers with the provided log_level
 */
void dump_stack(enum log_level, struct registers*);
