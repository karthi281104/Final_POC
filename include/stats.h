#ifndef STATS_H
#define STATS_H

#include "common.h"
#include "portable_pthread.h"

typedef struct {
    unsigned long searchCount;
    unsigned long updateCount;
    unsigned long cacheHits;
    unsigned long cacheMisses;
    pthread_mutex_t lock;
} Stats;

status_t statsInit(Stats *stats);
status_t statsDestroy(Stats *stats);

void statsRecordSearch(Stats *stats);
void statsRecordUpdate(Stats *stats);
void statsRecordCacheHit(Stats *stats);
void statsRecordCacheMiss(Stats *stats);

/* hitRatio = cacheHits / (cacheHits + cacheMisses), 0.0 if no lookups yet */
double statsGetHitRatio(Stats *stats);

typedef struct {
    unsigned long searchCount;
    unsigned long updateCount;
    unsigned long cacheHits;
    unsigned long cacheMisses;
    double hitRatio;
} StatsSnapshot;

status_t statsGetSnapshot(Stats *stats, StatsSnapshot *outSnap);

#endif /* STATS_H */
