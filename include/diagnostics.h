#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include "common.h"
#include "cache.h"
#include "searchcache.h"
#include "memory.h"

typedef struct {
    double addLatencyMs;
    double searchLatencyMs;
    double updateLatencyMs;
    size_t operationsRun;
} BenchmarkReport;

/* Runs `iterations` add/search/update cycles against scratch symbols in
 * the given main DB and reports average latency per operation. */
status_t diagRunBenchmark(MainCache *mainDb, size_t iterations, BenchmarkReport *outReport);

status_t diagPrintMemoryReport(void);
status_t diagPrintPerformanceReport(MainCache *mainDb, SearchCache *searchDb);

#endif /* DIAGNOSTICS_H */
