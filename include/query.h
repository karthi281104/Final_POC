#ifndef QUERY_H
#define QUERY_H

#include "common.h"
#include "cache.h"
#include "searchcache.h"
#include "stats.h"

/* Full "search a symbol" workflow:
 *  1. Validate the symbol format.
 *  2. Look it up in the search (LRU) cache first; if present, that's a
 *     cache HIT and we still refresh recency.
 *  3. Otherwise look it up in the main DB; if found, that's a cache
 *     MISS (but a main-DB hit) and we insert it into the search cache
 *     (possibly evicting the current LRU entry).
 *  4. Records stats and writes an access-log entry either way.
 *  5. Reports which of main/cache/both/neither the symbol was found in.
 */
status_t queryExecuteSearch(MainCache *mainDb, SearchCache *searchDb, Stats *stats,
                             const char *symbol, Stock *outStock,
                             location_status_t *outLocation);

/* Reports where a symbol currently lives without affecting cache
 * recency ordering, used by feed/update flows. */
location_status_t queryLocationStatus(MainCache *mainDb, SearchCache *searchDb,
                                       const char *symbol);

#endif /* QUERY_H */
