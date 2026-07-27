#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

#include "common.h"
#include "cache.h"
#include "searchcache.h"
#include "alerts.h"
#include "stats.h"
#include "portable_pthread.h"

typedef struct {
    MainCache *mainDb;
    SearchCache *searchDb;
    AlertStore *alerts;
    Stats *stats;

    pthread_t feedThread;
    pthread_t loggerThread;
    pthread_t statsThread;

    volatile int running; /* 0 = stop requested, 1 = running */
    pthread_mutex_t stateLock;
    pthread_cond_t stateCond;

    unsigned int feedIntervalSeconds;
    unsigned int heartbeatIntervalSeconds;
    unsigned int statsIntervalSeconds;
} ThreadManager;

status_t tmInit(ThreadManager *tm, MainCache *mainDb, SearchCache *searchDb,
                 AlertStore *alerts, Stats *stats);

/* Starts all three background threads (feed, logger heartbeat, stats
 * snapshot). Non-busy-waiting: each thread sleeps via a condition
 * variable timedwait so tmStopAll() can interrupt it immediately. */
status_t tmStartAll(ThreadManager *tm);

/* Signals all threads to stop and joins them. Safe to call once. */
status_t tmStopAll(ThreadManager *tm);

status_t tmDestroy(ThreadManager *tm);

#endif /* THREAD_MANAGER_H */
