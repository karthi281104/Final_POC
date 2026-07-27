#ifndef SEARCHCACHE_H
#define SEARCHCACHE_H

#include "common.h"
#include <stdbool.h>
#include "portable_pthread.h"

/* ===== SEARCH (LRU) CACHE ==============================================
 * Holds ONLY the SEARCH_CACHE_CAPACITY most recently searched symbols.
 * Implemented as a doubly linked list: head = most recently used,
 * tail = least recently used (evicted first). A parallel small array
 * would also work at this capacity, but the list makes move-to-front
 * O(1) and mirrors classic LRU design. */

typedef struct SearchCacheNode {
    Stock data;
    struct SearchCacheNode *prev;
    struct SearchCacheNode *next;
} SearchCacheNode;

typedef struct {
    SearchCacheNode *head; /* most recently used */
    SearchCacheNode *tail; /* least recently used */
    size_t count;
    pthread_mutex_t lock;
} SearchCache;

status_t searchCacheInit(SearchCache *sc);
status_t searchCacheDestroy(SearchCache *sc);

/* Insert-or-refresh a stock as most-recently-used. If at capacity and
 * the symbol is new, evicts the LRU entry first. */
status_t searchCacheTouch(SearchCache *sc, const Stock *stock);

/* Pure lookup, does NOT change recency order (use searchCacheTouch for that). */
status_t searchCacheSearch(SearchCache *sc, const char *symbol, Stock *outStock);

/* If the symbol is present, update its price (used to keep cache in
 * sync with the main DB per the two-database sync rule). */
status_t searchCacheUpdatePrice(SearchCache *sc, const char *symbol, double newPrice);

bool   searchCacheContains(SearchCache *sc, const char *symbol);
size_t searchCacheCount(SearchCache *sc);

/* Snapshot in MRU-to-LRU order into a heap array (mmAlloc'd). Caller
 * must mmFree(*outArray). */
status_t searchCacheSnapshot(SearchCache *sc, Stock **outArray, size_t *outCount);

#endif /* SEARCHCACHE_H */
