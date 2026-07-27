#include "testing.h"
#include "cache.h"
#include "searchcache.h"
#include "persistence.h"
#include "stats.h"
#include "security.h"
#include "alerts.h"
#include "memory.h"
#include "feed.h"
#include "query.h"
#include "logger.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "portable_pthread.h"
#include <stdlib.h>

#define SCRATCH_STOCK_PATH "data/.tester_scratch_stock.db"
#define SCRATCH_CACHE_PATH "data/.tester_scratch_cache.db"

static void reportCase(const char *category, const char *caseName, bool passed,
                        const char *expected, const char *actual, int *failCount)
{
    if (passed)
    {
        (void)printf("    [PASS] %s\n", caseName);
    }
    else
    {
        (void)printf("    [FAIL] %s -- expected: %s, got: %s\n", caseName, expected, actual);
        if (failCount != NULL)
        {
            (*failCount)++;
        }
    }
    (void)category; /* reserved for future grouped output */
}

/* ===================== 1. UNIT TESTING ================================= */

static int unitTest1_HashFunction(void)
{
    int fails = 0;
    unsigned long h1 = mainCacheHash("AAPL");
    unsigned long h2 = mainCacheHash("AAPL");
    unsigned long h3 = mainCacheHash("MSFT");
    bool deterministic = (h1 == h2);
    bool inRange = (h1 < MAIN_DB_BUCKETS) && (h3 < MAIN_DB_BUCKETS);

    (void)printf("Test 1: Hash Function Test\n");
    reportCase("unit", "Hash is deterministic for the same symbol", deterministic,
               "h1 == h2", deterministic ? "equal" : "different", &fails);
    reportCase("unit", "Hash values fall within bucket range", inRange,
               "< MAIN_DB_BUCKETS", inRange ? "in range" : "out of range", &fails);
    return fails;
}

static int unitTest2_MainCache(void)
{
    int fails = 0;
    MainCache mc;
    Stock st, found;
    status_t r;

    (void)printf("Test 2: Main Cache Test\n");
    (void)mainCacheInit(&mc);

    memset(&st, 0, sizeof(st));
    (void)safe_strcpy(st.symbol, sizeof(st.symbol), "ZZZT");
    (void)safe_strcpy(st.name, sizeof(st.name), "Zzz Test Corp");
    st.price = 50.0;

    r = mainCacheAdd(&mc, &st);
    reportCase("unit", "Add new stock succeeds", (r == STATUS_OK), "STATUS_OK",
               status_to_string(r), &fails);

    r = mainCacheSearch(&mc, "ZZZT", &found);
    reportCase("unit", "Search finds added stock", (r == STATUS_OK) && (found.price == 50.0),
               "found price 50.0", (r == STATUS_OK) ? "found" : "not found", &fails);

    r = mainCacheUpdatePrice(&mc, "ZZZT", 75.0);
    (void)mainCacheSearch(&mc, "ZZZT", &found);
    reportCase("unit", "Update price applies", (r == STATUS_OK) && (found.price == 75.0),
               "price == 75.0", (found.price == 75.0) ? "75.0" : "mismatch", &fails);

    r = mainCacheDelete(&mc, "ZZZT");
    reportCase("unit", "Delete removes stock", (r == STATUS_OK) && (!mainCacheContains(&mc, "ZZZT")),
               "not found after delete", mainCacheContains(&mc, "ZZZT") ? "still present" : "removed",
               &fails);

    (void)mainCacheDestroy(&mc);
    return fails;
}

static int unitTest3_SearchCacheLRU(void)
{
    int fails = 0;
    SearchCache sc;
    size_t i;
    status_t r;
    Stock found;

    (void)printf("Test 3: Search Cache (LRU) Test\n");
    (void)searchCacheInit(&sc);

    for (i = 0U; i < (SEARCH_CACHE_CAPACITY + 2U); i++)
    {
        Stock st;
        char symbolBuf[SYMBOL_MAX_LEN];
        memset(&st, 0, sizeof(st));
        (void)snprintf(symbolBuf, sizeof(symbolBuf), "S%03u", (unsigned)i);
        (void)safe_strcpy(st.symbol, sizeof(st.symbol), symbolBuf);
        (void)safe_strcpy(st.name, sizeof(st.name), "LRU Test");
        st.price = 1.0;
        (void)searchCacheTouch(&sc, &st);
    }

    reportCase("unit", "Cache caps at SEARCH_CACHE_CAPACITY after overflow inserts",
               (searchCacheCount(&sc) == SEARCH_CACHE_CAPACITY), "10", "varies", &fails);

    r = searchCacheSearch(&sc, "S000", &found); /* first-ever insert: should be evicted */
    reportCase("unit", "Oldest entry was evicted", (r != STATUS_OK), "NOT_FOUND",
               status_to_string(r), &fails);

    r = searchCacheSearch(&sc, "S011", &found); /* most recent insert: should remain */
    reportCase("unit", "Most recent entry still present", (r == STATUS_OK), "OK",
               status_to_string(r), &fails);

    /* Move-to-front: touch S002 again, then overflow once more; S002 should survive. */
    {
        Stock st;
        memset(&st, 0, sizeof(st));
        (void)safe_strcpy(st.symbol, sizeof(st.symbol), "S002");
        (void)safe_strcpy(st.name, sizeof(st.name), "LRU Test");
        st.price = 2.0;
        (void)searchCacheTouch(&sc, &st); /* refresh S002 to most-recently-used */
    }
    {
        Stock st;
        memset(&st, 0, sizeof(st));
        (void)safe_strcpy(st.symbol, sizeof(st.symbol), "SNEW");
        (void)safe_strcpy(st.name, sizeof(st.name), "LRU Test");
        st.price = 9.0;
        (void)searchCacheTouch(&sc, &st); /* forces one eviction: current LRU tail */
    }
    r = searchCacheSearch(&sc, "S002", &found);
    reportCase("unit", "Move-to-front protects recently-touched entry from eviction",
               (r == STATUS_OK), "OK", status_to_string(r), &fails);

    (void)searchCacheDestroy(&sc);
    return fails;
}

static int unitTest4_Persistence(void)
{
    int fails = 0;
    MainCache mc;
    SearchCache sc, scLoaded;
    Stock st, found;
    status_t r;

    (void)printf("Test 4: Persistence Test (scratch files only, never touches real DBs)\n");

    (void)mainCacheInit(&mc);
    memset(&st, 0, sizeof(st));
    (void)safe_strcpy(st.symbol, sizeof(st.symbol), "PSTX");
    (void)safe_strcpy(st.name, sizeof(st.name), "Persist Test Co");
    st.price = 33.5;
    (void)mainCacheAdd(&mc, &st);

    r = saveMainDbToPath(&mc, SCRATCH_STOCK_PATH);
    reportCase("unit", "Save main DB to scratch path", (r == STATUS_OK), "STATUS_OK",
               status_to_string(r), &fails);

    {
        MainCache mc2;
        (void)mainCacheInit(&mc2);
        r = loadMainDbFromPath(&mc2, SCRATCH_STOCK_PATH);
        (void)mainCacheSearch(&mc2, "PSTX", &found);
        reportCase("unit", "Reload main DB round-trips price correctly",
                   (r == STATUS_OK) && (found.price == 33.5),
                   "price == 33.5", (found.price == 33.5) ? "33.5" : "mismatch", &fails);
        (void)mainCacheDestroy(&mc2);
    }

    (void)searchCacheInit(&sc);
    (void)searchCacheTouch(&sc, &st);
    r = saveCacheToPath(&sc, SCRATCH_CACHE_PATH);
    reportCase("unit", "Save search cache to scratch path", (r == STATUS_OK), "STATUS_OK",
               status_to_string(r), &fails);

    (void)searchCacheInit(&scLoaded);
    r = loadCacheFromPath(&scLoaded, SCRATCH_CACHE_PATH);
    reportCase("unit", "Reload search cache round-trips entry",
               (r == STATUS_OK) && searchCacheContains(&scLoaded, "PSTX"),
               "PSTX present", searchCacheContains(&scLoaded, "PSTX") ? "present" : "missing",
               &fails);

    (void)mainCacheDestroy(&mc);
    (void)searchCacheDestroy(&sc);
    (void)searchCacheDestroy(&scLoaded);
    (void)remove(SCRATCH_STOCK_PATH);
    (void)remove(SCRATCH_CACHE_PATH);

    return fails;
}

static int unitTest5_Statistics(void)
{
    int fails = 0;
    Stats stats;
    double ratio;

    (void)printf("Test 5: Statistics Test\n");
    (void)statsInit(&stats);

    statsRecordSearch(&stats);
    statsRecordSearch(&stats);
    statsRecordCacheHit(&stats);
    statsRecordCacheHit(&stats);
    statsRecordCacheHit(&stats);
    statsRecordCacheMiss(&stats);
    statsRecordUpdate(&stats);

    ratio = statsGetHitRatio(&stats);
    /* 3 hits, 1 miss => 0.75 */
    reportCase("unit", "Hit ratio math is correct (3 hits / 1 miss = 0.75)",
               (ratio > 0.749) && (ratio < 0.751), "0.75", "computed", &fails);

    {
        StatsSnapshot snap;
        (void)statsGetSnapshot(&stats, &snap);
        reportCase("unit", "Counters match recorded operations",
                   (snap.searchCount == 2UL) && (snap.updateCount == 1UL) &&
                   (snap.cacheHits == 3UL) && (snap.cacheMisses == 1UL),
                   "search=2 update=1 hits=3 misses=1", "see counters", &fails);
    }

    (void)statsDestroy(&stats);
    return fails;
}

int testerRunUnitTests(void)
{
    int totalFails = 0;
    (void)printf("\n===== UNIT TESTING =====\n");
    totalFails += unitTest1_HashFunction();
    totalFails += unitTest2_MainCache();
    totalFails += unitTest3_SearchCacheLRU();
    totalFails += unitTest4_Persistence();
    totalFails += unitTest5_Statistics();
    (void)printf("Unit testing complete: %d failing check(s)\n", totalFails);
    return totalFails;
}

/* ===================== 2. INTEGRATION TESTING =========================== */

int testerRunIntegrationTests(void)
{
    int fails = 0;
    MainCache mc;
    SearchCache sc;
    AlertStore alerts;
    Stats stats;
    Stock st, found;
    status_t r;
    size_t triggered = 0U;

    (void)printf("\n===== INTEGRATION TESTING =====\n");

    (void)mainCacheInit(&mc);
    (void)searchCacheInit(&sc);
    (void)alertsInit(&alerts);
    (void)statsInit(&stats);

    memset(&st, 0, sizeof(st));
    (void)safe_strcpy(st.symbol, sizeof(st.symbol), "ITGX");
    (void)safe_strcpy(st.name, sizeof(st.name), "Integration Test Co");
    st.price = 100.0;

    r = mainCacheAdd(&mc, &st);
    reportCase("integration", "Step 1: add stock to main DB", (r == STATUS_OK), "STATUS_OK",
               status_to_string(r), &fails);

    {
        location_status_t loc;
        r = queryExecuteSearch(&mc, &sc, &stats, "ITGX", &found, &loc);
        reportCase("integration", "Step 2: search populates search cache",
                   (r == STATUS_OK) && searchCacheContains(&sc, "ITGX"),
                   "cache contains ITGX",
                   searchCacheContains(&sc, "ITGX") ? "present" : "missing", &fails);
    }

    r = alertsCreate(&alerts, "ITGX", 150.0, ALERT_ABOVE, "tester");
    reportCase("integration", "Step 3: create ABOVE alert at 150.0", (r == STATUS_OK),
               "STATUS_OK", status_to_string(r), &fails);

    r = feedApplyPriceUpdate(&mc, &sc, &alerts, &stats, "ITGX", 160.0);
    reportCase("integration", "Step 4: feed update to 160.0 succeeds", (r == STATUS_OK),
               "STATUS_OK", status_to_string(r), &fails);

    (void)mainCacheSearch(&mc, "ITGX", &found);
    reportCase("integration", "Step 5: main DB reflects new price",
               (found.price == 160.0), "160.0",
               (found.price == 160.0) ? "160.0" : "mismatch", &fails);

    (void)searchCacheSearch(&sc, "ITGX", &found);
    reportCase("integration", "Step 6: search cache reflects new price (sync rule)",
               (found.price == 160.0), "160.0",
               (found.price == 160.0) ? "160.0" : "mismatch", &fails);

    (void)alertsCheckPrice(&alerts, "ITGX", 160.0, &triggered);
    {
        Alert all[MAX_ALERTS];
        size_t count = 0U;
        bool wasTriggered = false;
        size_t i;
        (void)alertsGetAll(&alerts, all, MAX_ALERTS, &count);
        for (i = 0U; i < count; i++)
        {
            if ((safe_strcasecmp(all[i].symbol, "ITGX") == 0) && all[i].triggered)
            {
                wasTriggered = true;
            }
        }
        reportCase("integration", "Step 7: alert triggered by price update", wasTriggered,
                   "triggered == true", wasTriggered ? "true" : "false", &fails);
    }

    r = loggerLog(LOG_HISTORY, "Integration test marker entry");
    reportCase("integration", "Step 8: log entry written successfully", (r == STATUS_OK),
               "STATUS_OK", status_to_string(r), &fails);

    (void)mainCacheDestroy(&mc);
    (void)searchCacheDestroy(&sc);
    (void)alertsDestroy(&alerts);
    (void)statsDestroy(&stats);

    (void)printf("Integration testing complete: %d failing check(s)\n", fails);
    return fails;
}

/* ===================== 3. MEMORY LEAK TESTING ============================ */

int testerRunMemoryLeakTest(void)
{
    int fails = 0;
    MainCache mc;
    SearchCache sc;
    MemStats before, after;
    size_t i;

    (void)printf("\n===== MEMORY LEAK TESTING (in-app proxy) =====\n");
    (void)printf("Note: this is a fast in-app proxy check. For an authoritative leak\n");
    (void)printf("check, compile with -fsanitize=address,undefined (see README) or run\n");
    (void)printf("under valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes.\n");

    (void)mmGetStats(&before);

    (void)mainCacheInit(&mc);
    (void)searchCacheInit(&sc);

    for (i = 0U; i < 50U; i++)
    {
        Stock st;
        char symbolBuf[SYMBOL_MAX_LEN];
        memset(&st, 0, sizeof(st));
        (void)snprintf(symbolBuf, sizeof(symbolBuf), "M%04u", (unsigned)i);
        (void)safe_strcpy(st.symbol, sizeof(st.symbol), symbolBuf);
        (void)safe_strcpy(st.name, sizeof(st.name), "Mem Test");
        st.price = 1.0;
        (void)mainCacheAdd(&mc, &st);
        (void)searchCacheTouch(&sc, &st);
    }

    for (i = 0U; i < 50U; i++)
    {
        char symbolBuf[SYMBOL_MAX_LEN];
        (void)snprintf(symbolBuf, sizeof(symbolBuf), "M%04u", (unsigned)i);
        (void)mainCacheDelete(&mc, symbolBuf);
    }

    (void)mainCacheDestroy(&mc);
    (void)searchCacheDestroy(&sc);

    (void)mmGetStats(&after);

    {
        bool invariantHolds = (after.totalAllocations == (after.totalFrees + (unsigned long)after.activeNodes));
        bool nonNegativeActive = (after.activeNodes >= 0L);

        (void)printf("    totalAllocations=%lu totalFrees=%lu activeNodes=%ld\n",
                      after.totalAllocations, after.totalFrees, after.activeNodes);

        reportCase("memory", "totalAllocations == totalFrees + activeNodes", invariantHolds,
                   "equal", invariantHolds ? "equal" : "mismatch", &fails);
        reportCase("memory", "activeNodes is non-negative", nonNegativeActive,
                   ">= 0", nonNegativeActive ? "non-negative" : "negative", &fails);
        reportCase("memory", "This test's own allocations were fully freed",
                   (after.activeNodes == before.activeNodes),
                   "activeNodes unchanged by this test",
                   (after.activeNodes == before.activeNodes) ? "unchanged" : "leaked", &fails);
    }

    (void)printf("Memory leak testing complete: %d failing check(s)\n", fails);
    return fails;
}

/* ===================== 4. SAFETY AND SECURITY VALIDATION TESTING ========= */

int testerRunSecurityValidationTests(void)
{
    int fails = 0;

    (void)printf("\n===== SAFETY AND SECURITY VALIDATION TESTING =====\n");

    reportCase("security", "Valid symbol 'AAPL' accepted", secValidateSymbol("AAPL"),
               "true", "see result", &fails);
    reportCase("security", "Empty symbol rejected", !secValidateSymbol(""),
               "false", "see result", &fails);
    reportCase("security", "Symbol with invalid char '@' rejected", !secValidateSymbol("AB@L"),
               "false", "see result", &fails);
    reportCase("security", "Overlong symbol rejected", !secValidateSymbol("TOOLONGSYMBOL"),
               "false", "see result", &fails);

    reportCase("security", "Valid price 100.0 accepted", secValidatePrice(100.0),
               "true", "see result", &fails);
    reportCase("security", "Negative price rejected", !secValidatePrice(-5.0),
               "false", "see result", &fails);
    reportCase("security", "Zero price rejected", !secValidatePrice(0.0),
               "false", "see result", &fails);
    reportCase("security", "Absurdly large price rejected", !secValidatePrice(1.0e12),
               "false", "see result", &fails);

    reportCase("security", "Valid username 'admin_1' accepted", secValidateUsername("admin_1"),
               "true", "see result", &fails);
    reportCase("security", "Empty username rejected", !secValidateUsername(""),
               "false", "see result", &fails);
    reportCase("security", "Username with space rejected", !secValidateUsername("bad user"),
               "false", "see result", &fails);

    reportCase("security", "Valid password 'passw0rd' accepted", secValidatePassword("passw0rd"),
               "true", "see result", &fails);
    reportCase("security", "Too-short password rejected", !secValidatePassword("ab"),
               "false", "see result", &fails);
    reportCase("security", "Password containing whitespace rejected", !secValidatePassword("bad pass"),
               "false", "see result", &fails);

    reportCase("security", "Menu choice within bounds accepted",
               secValidateMenuChoice(3, 0, 10), "true", "see result", &fails);
    reportCase("security", "Menu choice below bounds rejected",
               !secValidateMenuChoice(-1, 0, 10), "false", "see result", &fails);
    reportCase("security", "Menu choice above bounds rejected",
               !secValidateMenuChoice(11, 0, 10), "false", "see result", &fails);

    reportCase("security", "Valid company name accepted", secValidateCompanyName("Apple Inc."),
               "true", "see result", &fails);
    reportCase("security", "Empty company name rejected", !secValidateCompanyName(""),
               "false", "see result", &fails);

    (void)printf("Security validation testing complete: %d failing check(s)\n", fails);
    return fails;
}

/* ===================== 5. SMART TESTING ================================== */

int testerRunSmartTesting(void)
{
    int unitFails, integrationFails, memoryFails, securityFails;
    int totalFails;

    (void)printf("\n########## SMART TESTING: RUNNING ALL CATEGORIES ##########\n");

    unitFails = testerRunUnitTests();
    integrationFails = testerRunIntegrationTests();
    memoryFails = testerRunMemoryLeakTest();
    securityFails = testerRunSecurityValidationTests();

    totalFails = unitFails + integrationFails + memoryFails + securityFails;

    (void)printf("\n========== SMART TESTING CONSOLIDATED REPORT ==========\n");
    (void)printf("  Unit Testing failures        : %d\n", unitFails);
    (void)printf("  Integration Testing failures : %d\n", integrationFails);
    (void)printf("  Memory Leak Testing failures : %d\n", memoryFails);
    (void)printf("  Security Validation failures : %d\n", securityFails);
    (void)printf("  ---------------------------------------\n");
    (void)printf("  TOTAL FAILING CHECKS         : %d\n", totalFails);
    (void)printf("  OVERALL VERDICT              : %s\n",
                  (totalFails == 0) ? "ALL TESTS PASSED" : "ONE OR MORE TESTS FAILED");
    (void)printf("=========================================================\n");

    return totalFails;
}

/* ===================== BENCHMARK: Hash Table vs Linear Search =========== */

typedef struct {
    char symbol[SYMBOL_MAX_LEN];
    double price;
} BenchFlatEntry;

static status_t benchLinearSearch(const BenchFlatEntry *arr, size_t n, const char *symbol,
                                   double *outPrice)
{
    status_t result = STATUS_ERR_NOT_FOUND;
    size_t i;
    for (i = 0U; i < n; i++)
    {
        if (safe_strcasecmp(arr[i].symbol, symbol) == 0)
        {
            *outPrice = arr[i].price;
            result = STATUS_OK;
            break;
        }
    }
    return result;
}

static double benchNowMs(void)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((double)ts.tv_sec * 1000.0) + ((double)ts.tv_nsec / 1000000.0);
}

#define BENCH_SEARCHES_PER_ROUND 2000U

static void benchRunOneSize(size_t n)
{
    MainCache mc;
    BenchFlatEntry *flat;
    size_t i;
    double t0, t1, hashMs, linearMs;
    char lookupSymbols[BENCH_SEARCHES_PER_ROUND][SYMBOL_MAX_LEN];

    (void)mainCacheInit(&mc);
    flat = (BenchFlatEntry *)mmAlloc(sizeof(BenchFlatEntry) * n);

    for (i = 0U; i < n; i++)
    {
        Stock st;
        char symbolBuf[SYMBOL_MAX_LEN];
        memset(&st, 0, sizeof(st));
        (void)snprintf(symbolBuf, sizeof(symbolBuf), "S%05u", (unsigned)(i % 99999U));
        (void)safe_strcpy(st.symbol, sizeof(st.symbol), symbolBuf);
        (void)safe_strcpy(st.name, sizeof(st.name), "Bench Co");
        st.price = (double)i;
        (void)mainCacheAdd(&mc, &st);

        (void)safe_strcpy(flat[i].symbol, sizeof(flat[i].symbol), symbolBuf);
        flat[i].price = (double)i;
    }

    /* Bias lookups toward the tail of the data set - the worst case
     * for linear search, which a hash lookup doesn't care about. */
    for (i = 0U; i < BENCH_SEARCHES_PER_ROUND; i++)
    {
        size_t idx = (n > 0U) ? ((n - 1U) - (i % n)) : 0U;
        (void)snprintf(lookupSymbols[i], sizeof(lookupSymbols[i]), "S%05u", (unsigned)(idx % 99999U));
    }

    t0 = benchNowMs();
    for (i = 0U; i < BENCH_SEARCHES_PER_ROUND; i++)
    {
        Stock found;
        (void)mainCacheSearch(&mc, lookupSymbols[i], &found);
    }
    t1 = benchNowMs();
    hashMs = t1 - t0;

    t0 = benchNowMs();
    for (i = 0U; i < BENCH_SEARCHES_PER_ROUND; i++)
    {
        double price;
        (void)benchLinearSearch(flat, n, lookupSymbols[i], &price);
    }
    t1 = benchNowMs();
    linearMs = t1 - t0;

    (void)printf("N=%7zu | hash avg: %9.5f us/search | linear avg: %10.5f us/search | linear is %8.1fx slower\n",
                 n,
                 (hashMs * 1000.0) / (double)BENCH_SEARCHES_PER_ROUND,
                 (linearMs * 1000.0) / (double)BENCH_SEARCHES_PER_ROUND,
                 (hashMs > 0.0) ? (linearMs / hashMs) : 0.0);

    mmFree(flat);
    (void)mainCacheDestroy(&mc);
}

void testerRunBenchmarkHashVsLinear(void)
{
    size_t sizes[] = {100U, 1000U, 5000U, 20000U, 50000U};
    size_t i;

    (void)printf("\n===== BENCHMARK: Hash Table (this project) vs Naive Linear Array Search =====\n");
    (void)printf("Each row: %u searches, biased toward entries near the end of the data set\n\n",
                 BENCH_SEARCHES_PER_ROUND);

    for (i = 0U; i < (sizeof(sizes) / sizeof(sizes[0])); i++)
    {
        benchRunOneSize(sizes[i]);
    }

    (void)printf("\nWhat this shows: hash search stays roughly FLAT no matter how much data\n");
    (void)printf("you add (O(1) average case). Linear search time grows proportionally with\n");
    (void)printf("N (O(n)) - that gap is the entire reason the main DB uses a hash table.\n");
}

/* ===================== BENCHMARK: rwlock vs mutex concurrency =========== */

#define RW_BUCKET_COUNT   101U
#define RW_SEED_COUNT     200U
#define RW_SEARCHES_PER_THREAD 20000U
#define RW_MAX_THREADS 16U

typedef struct RwBenchNode {
    char symbol[SYMBOL_MAX_LEN];
    double price;
    struct RwBenchNode *next;
} RwBenchNode;

typedef struct {
    RwBenchNode *buckets[RW_BUCKET_COUNT];
    pthread_rwlock_t rwLock;
    pthread_mutex_t muLock;
} RwBenchTable;

static unsigned long rwBenchHash(const char *s)
{
    unsigned long h = 5381UL;
    size_t i = 0U;
    while (s[i] != '\0')
    {
        h = ((h << 5) + h) + (unsigned long)(unsigned char)s[i];
        i++;
    }
    return h % RW_BUCKET_COUNT;
}

static void rwBenchSeed(RwBenchTable *table)
{
    size_t i;
    for (i = 0U; i < RW_SEED_COUNT; i++)
    {
        char symbolBuf[SYMBOL_MAX_LEN];
        RwBenchNode *n = (RwBenchNode *)mmAlloc(sizeof(RwBenchNode));
        (void)snprintf(symbolBuf, sizeof(symbolBuf), "R%04u", (unsigned)i);
        (void)safe_strcpy(n->symbol, sizeof(n->symbol), symbolBuf);
        n->price = (double)i;
        {
            unsigned long idx = rwBenchHash(symbolBuf);
            n->next = table->buckets[idx];
            table->buckets[idx] = n;
        }
    }
}

static void rwBenchFreeAll(RwBenchTable *table)
{
    size_t i;
    for (i = 0U; i < RW_BUCKET_COUNT; i++)
    {
        RwBenchNode *n = table->buckets[i];
        while (n != NULL)
        {
            RwBenchNode *next = n->next;
            mmFree(n);
            n = next;
        }
        table->buckets[i] = NULL;
    }
}

typedef struct {
    RwBenchTable *table;
    int useRwLock;
} RwBenchWorkerArg;

static void *rwBenchWorker(void *argPtr)
{
    RwBenchWorkerArg *arg = (RwBenchWorkerArg *)argPtr;
    size_t i;
    for (i = 0U; i < RW_SEARCHES_PER_THREAD; i++)
    {
        char symbolBuf[SYMBOL_MAX_LEN];
        unsigned long idx;
        (void)snprintf(symbolBuf, sizeof(symbolBuf), "R%04u", (unsigned)(i % RW_SEED_COUNT));
        idx = rwBenchHash(symbolBuf);

        if (arg->useRwLock)
        {
            (void)pthread_rwlock_rdlock(&arg->table->rwLock);
            {
                RwBenchNode *n = arg->table->buckets[idx];
                while (n != NULL)
                {
                    if (safe_strcasecmp(n->symbol, symbolBuf) == 0) { break; }
                    n = n->next;
                }
            }
            (void)pthread_rwlock_unlock(&arg->table->rwLock);
        }
        else
        {
            (void)pthread_mutex_lock(&arg->table->muLock);
            {
                RwBenchNode *n = arg->table->buckets[idx];
                while (n != NULL)
                {
                    if (safe_strcasecmp(n->symbol, symbolBuf) == 0) { break; }
                    n = n->next;
                }
            }
            (void)pthread_mutex_unlock(&arg->table->muLock);
        }
    }
    return NULL;
}

static double rwBenchRunVariant(RwBenchTable *table, int useRwLock, unsigned int numThreads)
{
    pthread_t threads[RW_MAX_THREADS];
    RwBenchWorkerArg args[RW_MAX_THREADS];
    unsigned int i;
    double t0, t1;
    double totalSearches = (double)numThreads * (double)RW_SEARCHES_PER_THREAD;

    t0 = benchNowMs();
    for (i = 0U; i < numThreads; i++)
    {
        args[i].table = table;
        args[i].useRwLock = useRwLock;
        (void)pthread_create(&threads[i], NULL, rwBenchWorker, &args[i]);
    }
    for (i = 0U; i < numThreads; i++)
    {
        (void)pthread_join(threads[i], NULL);
    }
    t1 = benchNowMs();

    return totalSearches / ((t1 - t0) / 1000.0);
}

void testerRunBenchmarkRwlockVsMutex(void)
{
    RwBenchTable *table = (RwBenchTable *)mmAlloc(sizeof(RwBenchTable));
    unsigned int threadCounts[] = {1U, 2U, 4U, 8U, 16U};
    size_t i;

    memset(table->buckets, 0, sizeof(table->buckets));
    (void)pthread_rwlock_init(&table->rwLock, NULL);
    (void)pthread_mutex_init(&table->muLock, NULL);
    rwBenchSeed(table);

    (void)printf("\n===== BENCHMARK: pthread_rwlock_t vs pthread_mutex_t under concurrent reads =====\n");
    (void)printf("(%u searches per thread, all threads searching simultaneously)\n\n", RW_SEARCHES_PER_THREAD);
    (void)printf("%-8s | %18s | %18s | %10s\n", "Threads", "rwlock (ops/sec)", "mutex (ops/sec)", "speedup");
    (void)printf("---------------------------------------------------------------------\n");

    for (i = 0U; i < (sizeof(threadCounts) / sizeof(threadCounts[0])); i++)
    {
        double rwThroughput = rwBenchRunVariant(table, 1, threadCounts[i]);
        double muThroughput = rwBenchRunVariant(table, 0, threadCounts[i]);
        (void)printf("%-8u | %18.0f | %18.0f | %9.2fx\n",
                     threadCounts[i], rwThroughput, muThroughput,
                     (muThroughput > 0.0) ? (rwThroughput / muThroughput) : 0.0);
    }

    (void)printf("\nNote: on a single-CPU-core machine, expect these two columns to look\n");
    (void)printf("nearly identical - only one thread can run at any given instant either\n");
    (void)printf("way. The rwlock's advantage (multiple readers on DIFFERENT cores at the\n");
    (void)printf("same instant) only becomes visible on a multi-core machine.\n");

    (void)pthread_rwlock_destroy(&table->rwLock);
    (void)pthread_mutex_destroy(&table->muLock);
    rwBenchFreeAll(table);
    mmFree(table);
}

/* ===================== DEMO: LRU vs FIFO cache behavior ================= */

typedef struct FifoDemoNode {
    Stock data;
    struct FifoDemoNode *prev;
    struct FifoDemoNode *next;
} FifoDemoNode;

typedef struct {
    FifoDemoNode *head;
    FifoDemoNode *tail;
    size_t count;
} FifoDemoCache;

static void fifoDemoInit(FifoDemoCache *fc)
{
    fc->head = NULL;
    fc->tail = NULL;
    fc->count = 0U;
}

static FifoDemoNode *fifoDemoFind(FifoDemoCache *fc, const char *symbol)
{
    FifoDemoNode *n = fc->head;
    while (n != NULL)
    {
        if (safe_strcasecmp(n->data.symbol, symbol) == 0) { return n; }
        n = n->next;
    }
    return NULL;
}

static void fifoDemoTouch(FifoDemoCache *fc, const Stock *stock)
{
    FifoDemoNode *existing = fifoDemoFind(fc, stock->symbol);

    if (existing != NULL)
    {
        /* Key difference from LRU: update value only, do NOT move
         * this node to the front. Its eviction position is unchanged
         * by being searched again. */
        existing->data = *stock;
        return;
    }

    if (fc->count >= SEARCH_CACHE_CAPACITY)
    {
        FifoDemoNode *lru = fc->tail;
        if (lru->prev != NULL) { lru->prev->next = NULL; } else { fc->head = NULL; }
        fc->tail = lru->prev;
        mmFree(lru);
        fc->count--;
    }

    {
        FifoDemoNode *node = (FifoDemoNode *)mmAlloc(sizeof(FifoDemoNode));
        node->data = *stock;
        node->prev = NULL;
        node->next = fc->head;
        if (fc->head != NULL) { fc->head->prev = node; }
        fc->head = node;
        if (fc->tail == NULL) { fc->tail = node; }
        fc->count++;
    }
}

static bool fifoDemoContains(FifoDemoCache *fc, const char *symbol)
{
    return (fifoDemoFind(fc, symbol) != NULL);
}

static void fifoDemoDestroyAll(FifoDemoCache *fc)
{
    FifoDemoNode *n = fc->head;
    while (n != NULL)
    {
        FifoDemoNode *next = n->next;
        mmFree(n);
        n = next;
    }
    fc->head = NULL;
    fc->tail = NULL;
    fc->count = 0U;
}

static void demoMakeStock(Stock *st, const char *symbol)
{
    memset(st, 0, sizeof(*st));
    (void)safe_strcpy(st->symbol, sizeof(st->symbol), symbol);
    (void)safe_strcpy(st->name, sizeof(st->name), "Demo Co");
    st->price = 1.0;
}

void testerRunDemoLruVsFifo(void)
{
    SearchCache lru;
    FifoDemoCache fifo;
    size_t i;

    (void)printf("\n===== DEMO: LRU cache (this project) vs a plain FIFO cache =====\n");
    (void)printf("Step 1: search AAPL, then 9 other distinct symbols (B001..B009)\n");
    (void)printf("        -> both caches are now full (10/10), AAPL is the OLDEST entry in both.\n\n");

    (void)searchCacheInit(&lru);
    fifoDemoInit(&fifo);

    {
        Stock st;
        demoMakeStock(&st, "AAPL");
        (void)searchCacheTouch(&lru, &st);
        fifoDemoTouch(&fifo, &st);
    }
    for (i = 1U; i <= 9U; i++)
    {
        char symbolBuf[SYMBOL_MAX_LEN];
        Stock st;
        (void)snprintf(symbolBuf, sizeof(symbolBuf), "B%03u", (unsigned)i);
        demoMakeStock(&st, symbolBuf);
        (void)searchCacheTouch(&lru, &st);
        fifoDemoTouch(&fifo, &st);
    }

    (void)printf("Step 2: search AAPL AGAIN (a fresh, active re-lookup)\n\n");
    {
        Stock st;
        demoMakeStock(&st, "AAPL");
        (void)searchCacheTouch(&lru, &st);
        fifoDemoTouch(&fifo, &st);
    }

    (void)printf("Step 3: search ONE more brand-new symbol (NEWX) -> forces exactly one eviction\n\n");
    {
        Stock st;
        demoMakeStock(&st, "NEWX");
        (void)searchCacheTouch(&lru, &st);
        fifoDemoTouch(&fifo, &st);
    }

    (void)printf("===== RESULT =====\n");
    (void)printf("LRU  cache still contains AAPL? %s\n",
                 searchCacheContains(&lru, "AAPL") ? "YES (correct - it was just re-searched)" : "NO");
    (void)printf("LRU  cache still contains B001? %s\n",
                 searchCacheContains(&lru, "B001") ? "YES" : "NO (correctly evicted - true LRU, never re-touched)");
    (void)printf("\n");
    (void)printf("FIFO cache still contains AAPL? %s\n",
                 fifoDemoContains(&fifo, "AAPL") ? "YES" : "NO (WRONG - evicted despite being just re-searched!)");
    (void)printf("FIFO cache still contains B001? %s\n",
                 fifoDemoContains(&fifo, "B001") ? "YES (still sitting there, untouched, while AAPL got evicted)" : "NO");

    (void)searchCacheDestroy(&lru);
    fifoDemoDestroyAll(&fifo);
}
