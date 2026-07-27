#include "alerts.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

status_t alertsInit(AlertStore *store)
{
    status_t result = STATUS_OK;
    if (store == NULL)
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        store->count = 0U;
        if (pthread_mutex_init(&store->lock, NULL) != 0)
        {
            result = STATUS_ERR_LOCK;
        }
    }
    return result;
}

status_t alertsDestroy(AlertStore *store)
{
    status_t result = STATUS_OK;
    if (store == NULL)
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        (void)pthread_mutex_destroy(&store->lock);
    }
    return result;
}

status_t alertsCreate(AlertStore *store, const char *symbol, double threshold,
                       alert_type_t type, const char *owner)
{
    status_t result = STATUS_OK;

    if ((store == NULL) || (symbol == NULL) || (owner == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        (void)pthread_mutex_lock(&store->lock);
        {
            if (store->count >= MAX_ALERTS)
            {
                result = STATUS_ERR_FULL;
            }
            else
            {
                Alert *a = &store->items[store->count];
                (void)safe_strcpy(a->symbol, sizeof(a->symbol), symbol);
                a->threshold = threshold;
                a->type = type;
                a->triggered = false;
                (void)safe_strcpy(a->owner, sizeof(a->owner), owner);
                store->count++;
            }
        }
        (void)pthread_mutex_unlock(&store->lock);
    }
    return result;
}

status_t alertsCheckPrice(AlertStore *store, const char *symbol, double currentPrice,
                           size_t *outTriggeredCount)
{
    status_t result = STATUS_OK;
    size_t triggered = 0U;

    if ((store == NULL) || (symbol == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        (void)pthread_mutex_lock(&store->lock);
        {
            size_t i;
            for (i = 0U; i < store->count; i++)
            {
                Alert *a = &store->items[i];
                if ((!a->triggered) && (safe_strcasecmp(a->symbol, symbol) == 0))
                {
                    bool fires = false;
                    if ((a->type == ALERT_ABOVE) && (currentPrice >= a->threshold))
                    {
                        fires = true;
                    }
                    else if ((a->type == ALERT_BELOW) && (currentPrice <= a->threshold))
                    {
                        fires = true;
                    }
                    else
                    {
                        fires = false;
                    }

                    if (fires)
                    {
                        a->triggered = true;
                        triggered++;
                        (void)loggerLog(LOG_HISTORY,
                            "ALERT TRIGGERED symbol=%s type=%s threshold=%.4f price=%.4f owner=%s",
                            a->symbol, (a->type == ALERT_ABOVE) ? "ABOVE" : "BELOW",
                            a->threshold, currentPrice, a->owner);
                    }
                }
            }
        }
        (void)pthread_mutex_unlock(&store->lock);
    }

    if (outTriggeredCount != NULL)
    {
        *outTriggeredCount = triggered;
    }
    return result;
}

status_t alertsSaveToPath(AlertStore *store, const char *path)
{
    status_t result = STATUS_OK;
    if ((store == NULL) || (path == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        FILE *fp = fopen(path, "w");
        if (fp == NULL)
        {
            result = STATUS_ERR_IO;
        }
        else
        {
            (void)pthread_mutex_lock(&store->lock);
            {
                size_t i;
                for (i = 0U; i < store->count; i++)
                {
                    Alert *a = &store->items[i];
                    (void)fprintf(fp, "%s|%.4f|%d|%d|%s\n", a->symbol, a->threshold,
                                  (int)a->type, (int)a->triggered, a->owner);
                }
            }
            (void)pthread_mutex_unlock(&store->lock);
            (void)fclose(fp);
        }
    }
    return result;
}

status_t alertsLoadFromPath(AlertStore *store, const char *path)
{
    status_t result = STATUS_OK;
    if ((store == NULL) || (path == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        FILE *fp = fopen(path, "r");
        if (fp == NULL)
        {
            result = STATUS_ERR_IO;
        }
        else
        {
            char line[LINE_MAX_LEN];
            while (fgets(line, (int)sizeof(line), fp) != NULL)
            {
                char *symbolTok, *thresholdTok, *typeTok, *trigTok, *ownerTok;
                size_t len = strlen(line);
                while ((len > 0U) && ((line[len-1U] == '\n') || (line[len-1U] == '\r')))
                {
                    line[len-1U] = '\0';
                    len--;
                }
                if (line[0] == '\0')
                {
                    continue;
                }
                symbolTok = strtok(line, "|");
                thresholdTok = strtok(NULL, "|");
                typeTok = strtok(NULL, "|");
                trigTok = strtok(NULL, "|");
                ownerTok = strtok(NULL, "|");

                if ((symbolTok != NULL) && (thresholdTok != NULL) && (typeTok != NULL)
                    && (store->count < MAX_ALERTS))
                {
                    Alert *a = &store->items[store->count];
                    (void)safe_strcpy(a->symbol, sizeof(a->symbol), symbolTok);
                    a->threshold = atof(thresholdTok);
                    a->type = (atoi(typeTok) == 0) ? ALERT_ABOVE : ALERT_BELOW;
                    a->triggered = (trigTok != NULL) && (atoi(trigTok) != 0);
                    (void)safe_strcpy(a->owner, sizeof(a->owner),
                                      (ownerTok != NULL) ? ownerTok : "unknown");
                    store->count++;
                }
            }
            (void)fclose(fp);
        }
    }
    return result;
}

status_t alertsSave(AlertStore *store)
{
    return alertsSaveToPath(store, DEFAULT_ALERTS_DB_PATH);
}

status_t alertsLoad(AlertStore *store)
{
    return alertsLoadFromPath(store, DEFAULT_ALERTS_DB_PATH);
}

size_t alertsCount(AlertStore *store)
{
    size_t count = 0U;
    if (store != NULL)
    {
        (void)pthread_mutex_lock(&store->lock);
        count = store->count;
        (void)pthread_mutex_unlock(&store->lock);
    }
    return count;
}

status_t alertsGetAll(AlertStore *store, Alert *outArray, size_t maxCount, size_t *outCount)
{
    status_t result = STATUS_OK;
    if ((store == NULL) || (outArray == NULL) || (outCount == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        (void)pthread_mutex_lock(&store->lock);
        {
            size_t n = (store->count < maxCount) ? store->count : maxCount;
            size_t i;
            for (i = 0U; i < n; i++)
            {
                outArray[i] = store->items[i];
            }
            *outCount = n;
        }
        (void)pthread_mutex_unlock(&store->lock);
    }
    return result;
}
