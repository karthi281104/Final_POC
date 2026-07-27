#ifndef PERSISTENCE_H
#define PERSISTENCE_H

#include "common.h"
#include "cache.h"
#include "searchcache.h"

/* All functions are path-parameterized so the in-app tester (and the
 * external test suite) can point at scratch files without ever
 * touching the real data/stock.db or data/cache.db. Format: a simple
 * pipe-delimited text line per stock: SYMBOL|NAME|PRICE|LASTUPDATED */

status_t saveMainDbToPath(MainCache *cache, const char *path);
status_t loadMainDbFromPath(MainCache *cache, const char *path);

status_t saveCacheToPath(SearchCache *sc, const char *path);
status_t loadCacheFromPath(SearchCache *sc, const char *path);

/* Convenience wrappers using the default data/ paths */
status_t saveMainDb(MainCache *cache);
status_t loadMainDb(MainCache *cache);
status_t saveCacheDb(SearchCache *sc);
status_t loadCacheDb(SearchCache *sc);

/* Seeds the main DB with SEED_STOCK_COUNT companies (only used when
 * stock.db does not yet exist / is empty on first run). */
status_t persistenceSeedMainDb(MainCache *cache);

#endif /* PERSISTENCE_H */
