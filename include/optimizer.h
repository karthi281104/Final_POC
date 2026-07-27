#ifndef OPTIMIZER_H
#define OPTIMIZER_H

#include "common.h"
#include "cache.h"
#include "searchcache.h"
#include "stats.h"
#include "alerts.h"

/* Read-only aggregate reports; never mutate any store. */
status_t optimizerPrintOptimizationReport(MainCache *mainDb, SearchCache *searchDb, Stats *stats);
status_t optimizerPrintSecurityReport(AlertStore *alerts);
status_t optimizerPrintSystemHealthReport(MainCache *mainDb, SearchCache *searchDb);

#endif /* OPTIMIZER_H */
