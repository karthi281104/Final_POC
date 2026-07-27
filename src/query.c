#include "query.h"
#include "security.h"
#include "logger.h"

location_status_t queryLocationStatus(MainCache *mainDb, SearchCache *searchDb,
                                       const char *symbol)
{
    location_status_t loc = LOC_NONE;
    bool inMain = mainCacheContains(mainDb, symbol);
    bool inCache = searchCacheContains(searchDb, symbol);

    if (inMain && inCache)
    {
        loc = LOC_BOTH;
    }
    else if (inMain)
    {
        loc = LOC_MAIN_ONLY;
    }
    else if (inCache)
    {
        loc = LOC_CACHE_ONLY;
    }
    else
    {
        loc = LOC_NONE;
    }
    return loc;
}

status_t queryExecuteSearch(MainCache *mainDb, SearchCache *searchDb, Stats *stats,
                             const char *symbol, Stock *outStock,
                             location_status_t *outLocation)
{
    status_t result;

    if ((mainDb == NULL) || (searchDb == NULL) || (stats == NULL) ||
        (symbol == NULL) || (outStock == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else if (!secValidateSymbol(symbol))
    {
        result = STATUS_ERR_INVALID_ARG;
        (void)loggerLog(LOG_ERROR, "Rejected invalid symbol in search: '%s'", symbol);
    }
    else
    {
        statsRecordSearch(stats);

        result = searchCacheSearch(searchDb, symbol, outStock);
        if (result == STATUS_OK)
        {
            /* Cache HIT: refresh recency */
            statsRecordCacheHit(stats);
            (void)searchCacheTouch(searchDb, outStock);
            if (outLocation != NULL)
            {
                *outLocation = LOC_BOTH; /* it's in cache, and cache entries always
                                              originate from main DB */
            }
            (void)loggerLog(LOG_ACCESS, "SEARCH symbol=%s result=CACHE_HIT", symbol);
        }
        else
        {
            statsRecordCacheMiss(stats);
            result = mainCacheSearch(mainDb, symbol, outStock);
            if (result == STATUS_OK)
            {
                (void)searchCacheTouch(searchDb, outStock);
                if (outLocation != NULL)
                {
                    *outLocation = LOC_MAIN_ONLY;
                }
                (void)loggerLog(LOG_ACCESS, "SEARCH symbol=%s result=MAIN_DB_HIT", symbol);
            }
            else
            {
                if (outLocation != NULL)
                {
                    *outLocation = LOC_NONE;
                }
                (void)loggerLog(LOG_ACCESS, "SEARCH symbol=%s result=NOT_FOUND", symbol);
            }
        }
    }
    return result;
}
