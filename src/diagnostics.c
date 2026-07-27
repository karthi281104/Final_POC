#include "diagnostics.h"
#include <stdio.h>
#include <time.h>
#include <string.h>

static double elapsedMs(struct timespec start, struct timespec end)
{
    double seconds = (double)(end.tv_sec - start.tv_sec);
    double nanos = (double)(end.tv_nsec - start.tv_nsec);
    return (seconds * 1000.0) + (nanos / 1000000.0);
}

status_t diagRunBenchmark(MainCache *mainDb, size_t iterations, BenchmarkReport *outReport)
{
    status_t result = STATUS_OK;

    if ((mainDb == NULL) || (outReport == NULL) || (iterations == 0U))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        struct timespec t0, t1;
        size_t i;
        double addTotal = 0.0;
        double searchTotal = 0.0;
        double updateTotal = 0.0;

        for (i = 0U; i < iterations; i++)
        {
            Stock st;
            Stock found;
            char symbolBuf[SYMBOL_MAX_LEN];

            memset(&st, 0, sizeof(st));
            (void)snprintf(symbolBuf, sizeof(symbolBuf), "B%05u", (unsigned)(i % 99999U));
            (void)safe_strcpy(st.symbol, sizeof(st.symbol), symbolBuf);
            (void)safe_strcpy(st.name, sizeof(st.name), "Benchmark Co");
            st.price = 100.0;
            st.lastUpdated = time(NULL);

            (void)clock_gettime(CLOCK_MONOTONIC, &t0);
            (void)mainCacheAdd(mainDb, &st);
            (void)clock_gettime(CLOCK_MONOTONIC, &t1);
            addTotal += elapsedMs(t0, t1);

            (void)clock_gettime(CLOCK_MONOTONIC, &t0);
            (void)mainCacheSearch(mainDb, st.symbol, &found);
            (void)clock_gettime(CLOCK_MONOTONIC, &t1);
            searchTotal += elapsedMs(t0, t1);

            (void)clock_gettime(CLOCK_MONOTONIC, &t0);
            (void)mainCacheUpdatePrice(mainDb, st.symbol, 101.0);
            (void)clock_gettime(CLOCK_MONOTONIC, &t1);
            updateTotal += elapsedMs(t0, t1);

            (void)mainCacheDelete(mainDb, st.symbol);
        }

        outReport->addLatencyMs = addTotal / (double)iterations;
        outReport->searchLatencyMs = searchTotal / (double)iterations;
        outReport->updateLatencyMs = updateTotal / (double)iterations;
        outReport->operationsRun = iterations;
    }
    return result;
}

status_t diagPrintMemoryReport(void)
{
    MemStats stats;
    status_t result = mmGetStats(&stats);
    if (result == STATUS_OK)
    {
        (void)printf("---- Memory Report ----\n");
        (void)printf("Total allocations : %lu\n", stats.totalAllocations);
        (void)printf("Total frees       : %lu\n", stats.totalFrees);
        (void)printf("Active nodes      : %ld\n", stats.activeNodes);
        (void)printf("Bytes allocated   : %zu\n", stats.bytesCurrentlyAllocated);
    }
    return result;
}

status_t diagPrintPerformanceReport(MainCache *mainDb, SearchCache *searchDb)
{
    status_t result = STATUS_OK;
    if ((mainDb == NULL) || (searchDb == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        BenchmarkReport report;
        result = diagRunBenchmark(mainDb, 50U, &report);
        if (result == STATUS_OK)
        {
            (void)printf("---- Performance Report ----\n");
            (void)printf("Main DB size        : %zu stocks\n", mainCacheCount(mainDb));
            (void)printf("Search cache size   : %zu / %u\n",
                          searchCacheCount(searchDb), (unsigned)SEARCH_CACHE_CAPACITY);
            (void)printf("Avg add latency     : %.5f ms\n", report.addLatencyMs);
            (void)printf("Avg search latency  : %.5f ms\n", report.searchLatencyMs);
            (void)printf("Avg update latency  : %.5f ms\n", report.updateLatencyMs);
        }
    }
    return result;
}
