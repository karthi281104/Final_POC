#ifndef LOGGER_H
#define LOGGER_H

#include "common.h"

typedef enum {
    LOG_ACCESS = 0,
    LOG_AUDIT = 1,
    LOG_ERROR = 2,
    LOG_HISTORY = 3
} log_type_t;

status_t loggerInit(void);
status_t loggerShutdown(void);

/* Thread-safe, timestamped append to the requested log file. */
status_t loggerLog(log_type_t type, const char *fmt, ...);

#endif /* LOGGER_H */
