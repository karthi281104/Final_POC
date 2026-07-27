#ifndef FEED_H
#define FEED_H

#include "common.h"
#include "cache.h"
#include "searchcache.h"
#include "alerts.h"
#include "stats.h"

/* Applies a validated price update to a symbol:
 *   1. Update the MAIN database (authoritative).
 *   2. If that symbol is currently present in the SEARCH cache, update
 *      it there too, so the two never go out of sync while cached.
 *   3. Check/trigger any alerts registered for that symbol.
 *   4. Log the update to the history log.
 * Used both by manual admin updates and by the background feed thread's
 * simulated ticks. */
status_t feedApplyPriceUpdate(MainCache *mainDb, SearchCache *searchDb, AlertStore *alerts,
                               Stats *stats, const char *symbol, double newPrice);

/* Produces one small random-walk tick for a randomly chosen existing
 * symbol in the main DB, used by the background feed thread. Returns
 * the symbol updated and its new price via out-params (optional). */
status_t feedSimulateTick(MainCache *mainDb, SearchCache *searchDb, AlertStore *alerts,
                           Stats *stats, char *outSymbol, size_t outSymbolSize,
                           double *outPrice);

#endif /* FEED_H */
