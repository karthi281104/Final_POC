#ifndef CACHE_H
#define CACHE_H

#include "common.h"
#include <stdbool.h>
#include "portable_pthread.h"

/* ===== MAIN DATABASE ===================================================
 * A hash table with separate chaining, keyed by symbol, protected by a
 * pthread_rwlock so multiple readers (searches) can proceed concurrently
 * while writers (add/update/delete) get exclusive access. This is the
 * single authoritative store for every stock the system knows about. */

typedef struct MainCacheNode {
    Stock data;
    struct MainCacheNode *next;
} MainCacheNode;

typedef struct {
    MainCacheNode *buckets[MAIN_DB_BUCKETS];
    size_t count;
    pthread_rwlock_t lock;
} MainCache;

status_t mainCacheInit(MainCache *cache);
status_t mainCacheDestroy(MainCache *cache);

status_t mainCacheAdd(MainCache *cache, const Stock *stock);
status_t mainCacheSearch(MainCache *cache, const char *symbol, Stock *outStock);
status_t mainCacheUpdatePrice(MainCache *cache, const char *symbol, double newPrice);
status_t mainCacheDelete(MainCache *cache, const char *symbol);
bool     mainCacheContains(MainCache *cache, const char *symbol);
size_t   mainCacheCount(MainCache *cache);

/* Snapshot all stocks into a heap-allocated (mmAlloc) array of length
 * *outCount. Caller must mmFree(*outArray). Used for "View All Stocks"
 * and for persistence. */
status_t mainCacheSnapshot(MainCache *cache, Stock **outArray, size_t *outCount);

unsigned long mainCacheHash(const char *symbol);

#endif /* CACHE_H */
