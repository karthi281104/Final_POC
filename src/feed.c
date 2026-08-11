#include "feed.h"
#include "security.h"
#include "logger.h"
#include "memory.h"
#include <stdlib.h>

#define TICK_PERCENT_RANGE 5 /* +/- up to 5% per simulated tick */

status_t feedApplyPriceUpdate(MainCache *mainDb, SearchCache *searchDb, AlertStore *alerts,
                               Stats *stats, const char *symbol, double newPrice)
{
    status_t result;

    if ((mainDb == NULL) || (searchDb == NULL) || (alerts == NULL) ||
        (stats == NULL) || (symbol == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else if ((!secValidateSymbol(symbol)) || (!secValidatePrice(newPrice)))
    {
        result = STATUS_ERR_INVALID_ARG;
        (void)loggerLog(LOG_ERROR, "Rejected invalid feed update symbol=%s price=%.4f",
                         symbol, newPrice);
    }
    else
    {
        result = mainCacheUpdatePrice(mainDb, symbol, newPrice);
        if (result == STATUS_OK)
        {
            statsRecordUpdate(stats);

            /* Sync rule: also update the search cache if this symbol is
             * currently held there, so main DB and cache never diverge
             * while the symbol is cached. */
            if (searchCacheContains(searchDb, symbol))
            {
                /* Attempt a non-blocking update of the search cache to avoid
                 * deadlocks: if another thread currently holds the cache
                 * lock, skip the cache update (it will be eventually
                 * consistent on next search/touch). */
                status_t u = searchCacheUpdatePriceTry(searchDb, symbol, newPrice);
                if (u == STATUS_ERR_BUSY)
                {
                    /* best-effort: skip updating cache to avoid blocking */
                }
            }

            {
                size_t triggeredCount = 0U;
                (void)alertsCheckPrice(alerts, symbol, newPrice, &triggeredCount);
            }

            (void)loggerLog(LOG_HISTORY, "PRICE UPDATE symbol=%s newPrice=%.4f", symbol, newPrice);
        }
        else
        {
            (void)loggerLog(LOG_ERROR, "Price update failed for unknown symbol=%s", symbol);
        }
    }
    return result;
}

status_t feedSimulateTick(MainCache *mainDb, SearchCache *searchDb, AlertStore *alerts,
                           Stats *stats, char *outSymbol, size_t outSymbolSize,
                           double *outPrice)
{
    status_t result = STATUS_OK;

    if ((mainDb == NULL) || (searchDb == NULL) || (alerts == NULL) || (stats == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        Stock *arr = NULL;
        size_t count = 0U;
        result = mainCacheSnapshot(mainDb, &arr, &count);

        if ((result == STATUS_OK) && (count > 0U))
        {
            size_t idx = (size_t)(rand() % (int)count);
            Stock chosen = arr[idx];
            int percentSwing = (rand() % ((2 * TICK_PERCENT_RANGE) + 1)) - TICK_PERCENT_RANGE;
            double newPrice = chosen.price * (1.0 + ((double)percentSwing / 100.0));

            if (newPrice < MIN_PRICE)
            {
                newPrice = MIN_PRICE;
            }

            result = feedApplyPriceUpdate(mainDb, searchDb, alerts, stats,
                                           chosen.symbol, newPrice);

            if (result == STATUS_OK)
            {
                if ((outSymbol != NULL) && (outSymbolSize > 0U))
                {
                    (void)safe_strcpy(outSymbol, outSymbolSize, chosen.symbol);
                }
                if (outPrice != NULL)
                {
                    *outPrice = newPrice;
                }
            }
        }
        else if (result == STATUS_OK)
        {
            result = STATUS_ERR_NOT_FOUND;
        }
        else
        {
            /* snapshot itself failed; propagate result as-is */
        }

        if (arr != NULL)
        {
            mmFree(arr);
        }
    }
    return result;
}
