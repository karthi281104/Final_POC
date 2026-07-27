#ifndef ALERTS_H
#define ALERTS_H

#include "common.h"
#include "portable_pthread.h"
#include <stdbool.h>

typedef enum {
    ALERT_ABOVE = 0,
    ALERT_BELOW = 1
} alert_type_t;

typedef struct {
    char symbol[SYMBOL_MAX_LEN];
    double threshold;
    alert_type_t type;
    bool triggered;
    char owner[USERNAME_MAX_LEN];
} Alert;

typedef struct {
    Alert items[MAX_ALERTS];
    size_t count;
    pthread_mutex_t lock;
} AlertStore;

status_t alertsInit(AlertStore *store);
status_t alertsDestroy(AlertStore *store);

status_t alertsCreate(AlertStore *store, const char *symbol, double threshold,
                       alert_type_t type, const char *owner);

/* Checks the given current price against every non-triggered alert for
 * that symbol; marks matching ones as triggered and logs them. Returns
 * the number of alerts triggered by this call via outTriggeredCount. */
status_t alertsCheckPrice(AlertStore *store, const char *symbol, double currentPrice,
                           size_t *outTriggeredCount);

status_t alertsSaveToPath(AlertStore *store, const char *path);
status_t alertsLoadFromPath(AlertStore *store, const char *path);
status_t alertsSave(AlertStore *store);
status_t alertsLoad(AlertStore *store);

size_t alertsCount(AlertStore *store);
status_t alertsGetAll(AlertStore *store, Alert *outArray, size_t maxCount, size_t *outCount);

#endif /* ALERTS_H */
