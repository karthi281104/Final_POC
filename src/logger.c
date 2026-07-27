#include "logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include "portable_pthread.h"

static pthread_mutex_t g_logLock;
static int g_ready = 0;

static const char *pathForType(log_type_t type)
{
    const char *path;
    switch (type)
    {
        case LOG_ACCESS:  path = ACCESS_LOG_PATH;  break;
        case LOG_AUDIT:   path = AUDIT_LOG_PATH;   break;
        case LOG_ERROR:   path = ERROR_LOG_PATH;   break;
        case LOG_HISTORY: path = HISTORY_LOG_PATH; break;
        default:          path = ERROR_LOG_PATH;   break;
    }
    return path;
}

status_t loggerInit(void)
{
    log_type_t t;
    (void)pthread_mutex_init(&g_logLock, NULL);
    g_ready = 1;

    /* Touch all four log files up front so the documented folder
     * structure (access/audit/error/history.log) always exists, even
     * if this run never happens to hit an error path. */
    for (t = LOG_ACCESS; t <= LOG_HISTORY; t++)
    {
        FILE *fp = fopen(pathForType(t), "a");
        if (fp != NULL)
        {
            (void)fclose(fp);
        }
    }

    return STATUS_OK;
}

status_t loggerShutdown(void)
{
    g_ready = 0;
    return STATUS_OK;
}

status_t loggerLog(log_type_t type, const char *fmt, ...)
{
    status_t result = STATUS_OK;

    if (fmt == NULL)
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        (void)pthread_mutex_lock(&g_logLock);
        {
            FILE *fp = fopen(pathForType(type), "a");
            if (fp == NULL)
            {
                result = STATUS_ERR_IO;
            }
            else
            {
                time_t now = time(NULL);
                char timeBuf[32];
                struct tm tmBuf;
                struct tm *tmPtr = localtime(&now);
                if (tmPtr != NULL)
                {
                    tmBuf = *tmPtr;
                }
                else
                {
                    memset(&tmBuf, 0, sizeof(tmBuf));
                }
                (void)strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tmBuf);

                (void)fprintf(fp, "[%s] ", timeBuf);

                {
                    va_list args;
                    va_start(args, fmt);
                    (void)vfprintf(fp, fmt, args);
                    va_end(args);
                }

                (void)fprintf(fp, "\n");
                (void)fclose(fp);
            }
        }
        (void)pthread_mutex_unlock(&g_logLock);
    }
    return result;
}
