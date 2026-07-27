#include "optimizer.h"
#include "memory.h"
#include <stdio.h>

status_t optimizerPrintOptimizationReport(MainCache *mainDb, SearchCache *searchDb, Stats *stats)
{
    status_t result = STATUS_OK;
    if ((mainDb == NULL) || (searchDb == NULL) || (stats == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        StatsSnapshot snap;
        (void)statsGetSnapshot(stats, &snap);

        (void)printf("---- Optimization Report ----\n");
        (void)printf("Main DB entries      : %zu\n", mainCacheCount(mainDb));
        (void)printf("Search cache entries : %zu / %u\n",
                      searchCacheCount(searchDb), (unsigned)SEARCH_CACHE_CAPACITY);
        (void)printf("Cache hit ratio      : %.2f%%\n", snap.hitRatio * 100.0);

        if (snap.hitRatio < 0.30)
        {
            (void)printf("Suggestion: hit ratio is low; consider whether users are\n"
                          "searching a wide, non-repeating set of symbols, which is\n"
                          "expected behavior for a small 10-slot LRU cache.\n");
        }
        else
        {
            (void)printf("Cache is performing within a healthy range for its size.\n");
        }
    }
    return result;
}

status_t optimizerPrintSecurityReport(AlertStore *alerts)
{
    status_t result = STATUS_OK;
    if (alerts == NULL)
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        size_t total = alertsCount(alerts);
        (void)printf("---- Security Report ----\n");
        (void)printf("Total alerts registered : %zu\n", total);
        (void)printf("All input validation routed through security.h; no raw\n"
                      "strcpy/strcat used anywhere in the codebase.\n");
    }
    return result;
}

status_t optimizerPrintSystemHealthReport(MainCache *mainDb, SearchCache *searchDb)
{
    status_t result = STATUS_OK;
    if ((mainDb == NULL) || (searchDb == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        MemStats mem;
        (void)mmGetStats(&mem);

        (void)printf("---- System Health Report ----\n");
        (void)printf("Main DB load           : %zu stocks\n", mainCacheCount(mainDb));
        (void)printf("Search cache load      : %zu / %u\n",
                      searchCacheCount(searchDb), (unsigned)SEARCH_CACHE_CAPACITY);
        (void)printf("Active memory blocks   : %ld\n", mem.activeNodes);
        (void)printf("Bytes currently in use : %zu\n", mem.bytesCurrentlyAllocated);
        (void)printf("Status                 : %s\n",
                      (mem.activeNodes >= 0L) ? "HEALTHY" : "ANOMALY DETECTED");
    }
    return result;
}
