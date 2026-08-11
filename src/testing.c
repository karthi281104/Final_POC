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
#include "auth.h"
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
    static const char * const syms[] = {
        "AAPL", "MSFT", "GOOGL", "A", "Z", "ZZZZZZ", "1", "ABCDEF", "NVDA", "JPM"
    };
    size_t i;
    unsigned long h;

    (void)printf("Test 1: Hash Function Test\n");

    /* Determinism + in-range for every symbol */
    for (i = 0U; i < (sizeof(syms)/sizeof(syms[0])); i++)
    {
        unsigned long h2;
        h  = mainCacheHash(syms[i]);
        h2 = mainCacheHash(syms[i]);
        reportCase("unit", "Hash deterministic", (h == h2), "equal", (h==h2)?"equal":"diff", &fails);
        reportCase("unit", "Hash in bucket range", (h < MAIN_DB_BUCKETS), "in range", (h<MAIN_DB_BUCKETS)?"ok":"out", &fails);
    }

    /* NULL input returns value still in range */
    h = mainCacheHash(NULL);
    reportCase("unit", "Hash(NULL) in range", (h < MAIN_DB_BUCKETS), "in range", (h<MAIN_DB_BUCKETS)?"ok":"out", &fails);

    /* Empty string in range */
    h = mainCacheHash("");
    reportCase("unit", "Hash(\"\") in range", (h < MAIN_DB_BUCKETS), "in range", (h<MAIN_DB_BUCKETS)?"ok":"out", &fails);

    /* Different symbols produce different hashes (collision not guaranteed,
     * but AAPL and MSFT are known to differ under djb2) */
    {
        unsigned long hA = mainCacheHash("AAPL");
        unsigned long hM = mainCacheHash("MSFT");
        reportCase("unit", "AAPL and MSFT hash differently", (hA != hM), "different", (hA!=hM)?"diff":"same", &fails);
    }
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

    /* ---- NULL arg guards ---- */
    r = mainCacheAdd(NULL, &st);
    reportCase("unit", "Add NULL cache => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
    r = mainCacheAdd(&mc, NULL);
    reportCase("unit", "Add NULL stock => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
    r = mainCacheSearch(NULL, "X", &found);
    reportCase("unit", "Search NULL cache => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
    r = mainCacheSearch(&mc, NULL, &found);
    reportCase("unit", "Search NULL symbol => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
    r = mainCacheSearch(&mc, "X", NULL);
    reportCase("unit", "Search NULL out => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
    r = mainCacheUpdatePrice(NULL, "X", 1.0);
    reportCase("unit", "UpdatePrice NULL cache => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
    r = mainCacheUpdatePrice(&mc, NULL, 1.0);
    reportCase("unit", "UpdatePrice NULL symbol => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
    r = mainCacheDelete(NULL, "X");
    reportCase("unit", "Delete NULL cache => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
    r = mainCacheDelete(&mc, NULL);
    reportCase("unit", "Delete NULL symbol => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);

    /* ---- count on empty cache ---- */
    reportCase("unit", "Count empty cache == 0", (mainCacheCount(&mc)==0U), "0", "see count", &fails);
    reportCase("unit", "Contains on empty => false", !mainCacheContains(&mc,"ZZZT"), "false", "ok", &fails);

    /* ---- not-found error paths ---- */
    r = mainCacheSearch(&mc, "ZZZT", &found);
    reportCase("unit", "Search missing stock => NOT_FOUND", (r==STATUS_ERR_NOT_FOUND), "NOT_FOUND", status_to_string(r), &fails);
    r = mainCacheUpdatePrice(&mc, "ZZZT", 10.0);
    reportCase("unit", "UpdatePrice missing stock => NOT_FOUND", (r==STATUS_ERR_NOT_FOUND), "NOT_FOUND", status_to_string(r), &fails);
    r = mainCacheDelete(&mc, "ZZZT");
    reportCase("unit", "Delete missing stock => NOT_FOUND", (r==STATUS_ERR_NOT_FOUND), "NOT_FOUND", status_to_string(r), &fails);

    /* ---- normal add / search / update / delete ---- */
    memset(&st, 0, sizeof(st));
    (void)safe_strcpy(st.symbol, sizeof(st.symbol), "ZZZT");
    (void)safe_strcpy(st.name, sizeof(st.name), "Zzz Test Corp");
    st.price = 50.0;
    r = mainCacheAdd(&mc, &st);
    reportCase("unit", "Add new stock => OK", (r==STATUS_OK), "STATUS_OK", status_to_string(r), &fails);
    reportCase("unit", "Count after add == 1", (mainCacheCount(&mc)==1U), "1", "see count", &fails);
    reportCase("unit", "Contains after add => true", mainCacheContains(&mc,"ZZZT"), "true", "ok", &fails);

    /* duplicate add */
    r = mainCacheAdd(&mc, &st);
    reportCase("unit", "Add duplicate => DUPLICATE", (r==STATUS_ERR_DUPLICATE), "DUPLICATE", status_to_string(r), &fails);
    reportCase("unit", "Count unchanged after dup add", (mainCacheCount(&mc)==1U), "1", "see count", &fails);

    r = mainCacheSearch(&mc, "ZZZT", &found);
    reportCase("unit", "Search finds added stock", (r==STATUS_OK)&&(found.price==50.0), "50.0", (r==STATUS_OK)?"found":"nf", &fails);

    /* Search exact symbol case */
    r = mainCacheSearch(&mc, "ZZZT", &found);
    reportCase("unit", "Search exact symbol case => OK", (r==STATUS_OK), "OK", status_to_string(r), &fails);

    r = mainCacheUpdatePrice(&mc, "ZZZT", 75.0);
    (void)mainCacheSearch(&mc, "ZZZT", &found);
    reportCase("unit", "UpdatePrice applies", (r==STATUS_OK)&&(found.price==75.0), "75.0", (found.price==75.0)?"ok":"mismatch", &fails);

    /* ---- snapshot ---- */
    {
        Stock *arr = NULL;
        size_t cnt = 0U;
        r = mainCacheSnapshot(&mc, &arr, &cnt);
        reportCase("unit", "Snapshot OK", (r==STATUS_OK), "OK", status_to_string(r), &fails);
        reportCase("unit", "Snapshot count == 1", (cnt==1U), "1", "see cnt", &fails);
        if (arr != NULL) { mmFree(arr); }
    }
    /* snapshot with NULL args */
    {
        Stock *arr2 = NULL; size_t cnt2 = 0U;
        r = mainCacheSnapshot(NULL, &arr2, &cnt2);
        reportCase("unit", "Snapshot NULL cache => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
        r = mainCacheSnapshot(&mc, NULL, &cnt2);
        reportCase("unit", "Snapshot NULL arr => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
        r = mainCacheSnapshot(&mc, &arr2, NULL);
        reportCase("unit", "Snapshot NULL cnt => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
    }

    /* ---- add second stock (collision chain exercise) ---- */
    {
        Stock st2;
        memset(&st2, 0, sizeof(st2));
        (void)safe_strcpy(st2.symbol, sizeof(st2.symbol), "ZZZX");
        (void)safe_strcpy(st2.name, sizeof(st2.name), "Chain Test");
        st2.price = 11.0;
        (void)mainCacheAdd(&mc, &st2);
        reportCase("unit", "Count after 2nd add == 2", (mainCacheCount(&mc)==2U), "2", "see count", &fails);
        /* delete non-head node */
        r = mainCacheDelete(&mc, "ZZZX");
        reportCase("unit", "Delete 2nd stock OK", (r==STATUS_OK), "OK", status_to_string(r), &fails);
        reportCase("unit", "First stock intact after delete of 2nd", mainCacheContains(&mc,"ZZZT"), "true", "ok", &fails);
    }

    /* delete head node */
    r = mainCacheDelete(&mc, "ZZZT");
    reportCase("unit", "Delete head stock => OK", (r==STATUS_OK), "OK", status_to_string(r), &fails);
    reportCase("unit", "Not present after delete", !mainCacheContains(&mc,"ZZZT"), "false", "ok", &fails);
    reportCase("unit", "Count == 0 after all deletes", (mainCacheCount(&mc)==0U), "0", "see count", &fails);

    /* snapshot on empty cache */
    {
        Stock *arr = NULL; size_t cnt = 99U;
        r = mainCacheSnapshot(&mc, &arr, &cnt);
        reportCase("unit", "Snapshot empty cache => OK", (r==STATUS_OK), "OK", status_to_string(r), &fails);
        reportCase("unit", "Snapshot empty gives cnt==0", (cnt==0U), "0", "see cnt", &fails);
        reportCase("unit", "Snapshot empty gives NULL arr", (arr==NULL), "NULL", "ok", &fails);
    }

    (void)mainCacheDestroy(&mc);
    /* destroy NULL */
    r = mainCacheDestroy(NULL);
    reportCase("unit", "Destroy NULL => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
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

    /* NULL init guard */
    r = searchCacheInit(NULL);
    reportCase("unit", "SearchCacheInit NULL => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);

    (void)searchCacheInit(&sc);

    /* NULL arg guards for touch/search/updatePrice */
    r = searchCacheTouch(NULL, NULL);
    reportCase("unit", "Touch NULL sc => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
    r = searchCacheSearch(NULL, "X", &found);
    reportCase("unit", "Search NULL sc => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
    r = searchCacheSearch(&sc, NULL, &found);
    reportCase("unit", "Search NULL sym => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
    r = searchCacheSearch(&sc, "X", NULL);
    reportCase("unit", "Search NULL out => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
    r = searchCacheUpdatePrice(NULL, "X", 1.0);
    reportCase("unit", "UpdatePrice NULL sc => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
    r = searchCacheUpdatePrice(&sc, NULL, 1.0);
    reportCase("unit", "UpdatePrice NULL sym => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);

    /* count on empty */
    reportCase("unit", "Count empty sc == 0", (searchCacheCount(&sc)==0U), "0", "ok", &fails);
    reportCase("unit", "Contains empty => false", !searchCacheContains(&sc,"AAPL"), "false", "ok", &fails);

    /* search miss on empty */
    r = searchCacheSearch(&sc, "AAPL", &found);
    reportCase("unit", "Search empty => NOT_FOUND", (r==STATUS_ERR_NOT_FOUND), "NOT_FOUND", status_to_string(r), &fails);

    /* updatePrice miss */
    r = searchCacheUpdatePrice(&sc, "AAPL", 200.0);
    reportCase("unit", "UpdatePrice missing => NOT_FOUND", (r==STATUS_ERR_NOT_FOUND), "NOT_FOUND", status_to_string(r), &fails);

    /* insert one and updatePrice */
    {
        Stock st;
        memset(&st, 0, sizeof(st));
        (void)safe_strcpy(st.symbol, sizeof(st.symbol), "AAPL");
        (void)safe_strcpy(st.name, sizeof(st.name), "Apple");
        st.price = 100.0;
        (void)searchCacheTouch(&sc, &st);
        r = searchCacheUpdatePrice(&sc, "AAPL", 200.0);
        reportCase("unit", "UpdatePrice existing => OK", (r==STATUS_OK), "OK", status_to_string(r), &fails);
        r = searchCacheSearch(&sc, "AAPL", &found);
        reportCase("unit", "Price updated in cache", (r==STATUS_OK)&&(found.price==200.0), "200.0", (r==STATUS_OK)?"ok":"nf", &fails);
    }

    /* touch existing = move-to-front, update value */
    {
        Stock st2;
        memset(&st2, 0, sizeof(st2));
        (void)safe_strcpy(st2.symbol, sizeof(st2.symbol), "AAPL");
        (void)safe_strcpy(st2.name, sizeof(st2.name), "Apple v2");
        st2.price = 300.0;
        r = searchCacheTouch(&sc, &st2);
        reportCase("unit", "Touch existing => OK", (r==STATUS_OK), "OK", status_to_string(r), &fails);
        r = searchCacheSearch(&sc, "AAPL", &found);
        reportCase("unit", "Touch updates value", (r==STATUS_OK)&&(found.price==300.0), "300.0", (r==STATUS_OK)?"ok":"nf", &fails);
    }

    /* snapshot one-element cache */
    {
        Stock *arr = NULL; size_t cnt = 0U;
        r = searchCacheSnapshot(&sc, &arr, &cnt);
        reportCase("unit", "Snapshot 1-elem OK", (r==STATUS_OK)&&(cnt==1U), "OK,cnt=1", status_to_string(r), &fails);
        if (arr != NULL) { mmFree(arr); }
    }
    /* snapshot NULL args */
    {
        Stock *a2 = NULL; size_t c2 = 0U;
        r = searchCacheSnapshot(NULL, &a2, &c2);
        reportCase("unit", "Snapshot NULL sc => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
        r = searchCacheSnapshot(&sc, NULL, &c2);
        reportCase("unit", "Snapshot NULL arr => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
        r = searchCacheSnapshot(&sc, &a2, NULL);
        reportCase("unit", "Snapshot NULL cnt => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
    }

    (void)searchCacheDestroy(&sc);

    /* fresh cache for LRU eviction tests */
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

    r = searchCacheSearch(&sc, "S000", &found);
    reportCase("unit", "Oldest entry was evicted", (r != STATUS_OK), "NOT_FOUND", status_to_string(r), &fails);

    r = searchCacheSearch(&sc, "S011", &found);
    reportCase("unit", "Most recent entry still present", (r == STATUS_OK), "OK", status_to_string(r), &fails);

    {
        Stock st;
        memset(&st, 0, sizeof(st));
        (void)safe_strcpy(st.symbol, sizeof(st.symbol), "S002");
        (void)safe_strcpy(st.name, sizeof(st.name), "LRU Test");
        st.price = 2.0;
        (void)searchCacheTouch(&sc, &st);
    }
    {
        Stock st;
        memset(&st, 0, sizeof(st));
        (void)safe_strcpy(st.symbol, sizeof(st.symbol), "SNEW");
        (void)safe_strcpy(st.name, sizeof(st.name), "LRU Test");
        st.price = 9.0;
        (void)searchCacheTouch(&sc, &st);
    }
    r = searchCacheSearch(&sc, "S002", &found);
    reportCase("unit", "Move-to-front protects recently-touched entry from eviction",
               (r == STATUS_OK), "OK", status_to_string(r), &fails);

    /* destroy NULL */
    r = searchCacheDestroy(NULL);
    reportCase("unit", "SearchCacheDestroy NULL => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);

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

    /* NULL arg guards */
    r = saveMainDbToPath(NULL, SCRATCH_STOCK_PATH);
    reportCase("unit", "saveMainDbToPath NULL cache => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
    r = saveMainDbToPath((MainCache*)1, NULL);
    reportCase("unit", "saveMainDbToPath NULL path => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
    r = loadMainDbFromPath(NULL, SCRATCH_STOCK_PATH);
    reportCase("unit", "loadMainDbFromPath NULL cache => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
    r = saveCacheToPath(NULL, SCRATCH_CACHE_PATH);
    reportCase("unit", "saveCacheToPath NULL sc => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
    r = loadCacheFromPath(NULL, SCRATCH_CACHE_PATH);
    reportCase("unit", "loadCacheFromPath NULL sc => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);

    /* bad path (unwritable directory) */
    (void)mainCacheInit(&mc);
    r = loadMainDbFromPath(&mc, "/no/such/path/file.db");
    reportCase("unit", "loadMainDbFromPath bad path => IO_ERROR", (r==STATUS_ERR_IO), "IO_ERROR", status_to_string(r), &fails);
    r = saveMainDbToPath(&mc, "/no/such/path/file.db");
    reportCase("unit", "saveMainDbToPath bad path => IO_ERROR", (r==STATUS_ERR_IO), "IO_ERROR", status_to_string(r), &fails);

    /* add multiple stocks and verify all round-trip */
    {
        Stock st2;
        memset(&st2, 0, sizeof(st2));
        (void)safe_strcpy(st2.symbol, sizeof(st2.symbol), "PSTX");
        (void)safe_strcpy(st2.name, sizeof(st2.name), "Persist Test Co");
        st2.price = 33.5;
        (void)mainCacheAdd(&mc, &st2);

        memset(&st2, 0, sizeof(st2));
        (void)safe_strcpy(st2.symbol, sizeof(st2.symbol), "TSTB");
        (void)safe_strcpy(st2.name, sizeof(st2.name), "Test B Corp");
        st2.price = 99.0;
        (void)mainCacheAdd(&mc, &st2);
    }

    r = saveMainDbToPath(&mc, SCRATCH_STOCK_PATH);
    reportCase("unit", "Save main DB to scratch path", (r == STATUS_OK), "STATUS_OK", status_to_string(r), &fails);

    {
        MainCache mc2;
        (void)mainCacheInit(&mc2);
        r = loadMainDbFromPath(&mc2, SCRATCH_STOCK_PATH);
        reportCase("unit", "Load main DB OK", (r==STATUS_OK), "OK", status_to_string(r), &fails);
        (void)mainCacheSearch(&mc2, "PSTX", &found);
        reportCase("unit", "Reload PSTX price", (found.price==33.5), "33.5", (found.price==33.5)?"ok":"mismatch", &fails);
        reportCase("unit", "Reload TSTB present", mainCacheContains(&mc2, "TSTB"), "true", "ok", &fails);
        (void)mainCacheDestroy(&mc2);
    }

    (void)searchCacheInit(&sc);
    memset(&st, 0, sizeof(st));
    (void)safe_strcpy(st.symbol, sizeof(st.symbol), "PSTX");
    (void)safe_strcpy(st.name, sizeof(st.name), "Persist Test Co");
    st.price = 33.5;
    (void)searchCacheTouch(&sc, &st);

    /* bad path for cache save */
    r = saveCacheToPath(&sc, "/no/such/path/cache.db");
    reportCase("unit", "saveCacheToPath bad path => IO_ERROR", (r==STATUS_ERR_IO), "IO_ERROR", status_to_string(r), &fails);

    r = saveCacheToPath(&sc, SCRATCH_CACHE_PATH);
    reportCase("unit", "Save search cache to scratch path", (r == STATUS_OK), "STATUS_OK", status_to_string(r), &fails);

    (void)searchCacheInit(&scLoaded);
    r = loadCacheFromPath(&scLoaded, SCRATCH_CACHE_PATH);
    reportCase("unit", "Reload search cache round-trips entry",
               (r == STATUS_OK) && searchCacheContains(&scLoaded, "PSTX"),
               "PSTX present", searchCacheContains(&scLoaded, "PSTX") ? "present" : "missing", &fails);

    /* bad path for cache load */
    {
        SearchCache scBad;
        (void)searchCacheInit(&scBad);
        r = loadCacheFromPath(&scBad, "/no/such/path/cache.db");
        reportCase("unit", "loadCacheFromPath bad path => IO_ERROR", (r==STATUS_ERR_IO), "IO_ERROR", status_to_string(r), &fails);
        (void)searchCacheDestroy(&scBad);
    }

    /* empty cache save/load */
    {
        SearchCache scEmpty, scReloaded;
        (void)searchCacheInit(&scEmpty);
        r = saveCacheToPath(&scEmpty, SCRATCH_CACHE_PATH);
        reportCase("unit", "Save empty search cache => OK", (r==STATUS_OK), "OK", status_to_string(r), &fails);
        (void)searchCacheInit(&scReloaded);
        r = loadCacheFromPath(&scReloaded, SCRATCH_CACHE_PATH);
        reportCase("unit", "Load empty search cache => OK", (r==STATUS_OK), "OK", status_to_string(r), &fails);
        reportCase("unit", "Empty cache reloads as empty", (searchCacheCount(&scReloaded)==0U), "0", "ok", &fails);
        (void)searchCacheDestroy(&scEmpty);
        (void)searchCacheDestroy(&scReloaded);
    }

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
    status_t r;

    (void)printf("Test 5: Statistics Test\n");

    /* NULL init/destroy guards */
    r = statsInit(NULL);
    reportCase("unit", "statsInit NULL => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);
    r = statsDestroy(NULL);
    reportCase("unit", "statsDestroy NULL => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);

    /* NULL snapshot arg */
    r = statsGetSnapshot(NULL, NULL);
    reportCase("unit", "statsGetSnapshot NULL stats => INVALID_ARG", (r==STATUS_ERR_INVALID_ARG), "INVALID_ARG", status_to_string(r), &fails);

    /* zero-division: no hits or misses => ratio == 0.0 */
    (void)statsInit(&stats);
    ratio = statsGetHitRatio(&stats);
    reportCase("unit", "Hit ratio with no lookups == 0.0", (ratio == 0.0), "0.0", (ratio==0.0)?"ok":"nonzero", &fails);

    /* NULL pass-through (should be silent no-ops) */
    statsRecordSearch(NULL);
    statsRecordUpdate(NULL);
    statsRecordCacheHit(NULL);
    statsRecordCacheMiss(NULL);
    reportCase("unit", "NULL stat ops are no-ops (no crash)", true, "no crash", "ok", &fails);

    /* snapshot with only misses => ratio == 0.0 */
    statsRecordCacheMiss(&stats);
    statsRecordCacheMiss(&stats);
    {
        StatsSnapshot snap;
        (void)statsGetSnapshot(&stats, &snap);
        reportCase("unit", "All-miss ratio == 0.0", (snap.hitRatio == 0.0), "0.0", (snap.hitRatio==0.0)?"ok":"nonzero", &fails);
        reportCase("unit", "Snapshot cacheMisses == 2", (snap.cacheMisses==2UL), "2", "ok", &fails);
    }
    (void)statsDestroy(&stats);

    /* main counting test */
    (void)statsInit(&stats);
    statsRecordSearch(&stats);
    statsRecordSearch(&stats);
    statsRecordCacheHit(&stats);
    statsRecordCacheHit(&stats);
    statsRecordCacheHit(&stats);
    statsRecordCacheMiss(&stats);
    statsRecordUpdate(&stats);

    ratio = statsGetHitRatio(&stats);
    reportCase("unit", "Hit ratio 3/4 == 0.75",
               (ratio > 0.749) && (ratio < 0.751), "0.75", "computed", &fails);

    {
        StatsSnapshot snap;
        r = statsGetSnapshot(&stats, &snap);
        reportCase("unit", "statsGetSnapshot => OK", (r==STATUS_OK), "OK", status_to_string(r), &fails);
        reportCase("unit", "Counters match operations",
                   (snap.searchCount==2UL)&&(snap.updateCount==1UL)&&
                   (snap.cacheHits==3UL)&&(snap.cacheMisses==1UL),
                   "search=2 update=1 hits=3 misses=1", "see counters", &fails);
        reportCase("unit", "Snapshot hitRatio matches direct query",
                   (snap.hitRatio > 0.749) && (snap.hitRatio < 0.751), "0.75", "ok", &fails);
    }

    /* getHitRatio with only hits => ratio == 1.0 */
    (void)statsDestroy(&stats);
    (void)statsInit(&stats);
    statsRecordCacheHit(&stats);
    ratio = statsGetHitRatio(&stats);
    reportCase("unit", "All-hit ratio == 1.0", (ratio > 0.999), "1.0", (ratio>0.999)?"ok":"low", &fails);

    (void)statsDestroy(&stats);
    return fails;
}

/* ===================== UNIT TEST 6: Alerts ============================= */
static int unitTest6_Alerts(void)
{
    int fails = 0;
    AlertStore store;
    Alert all[MAX_ALERTS];
    size_t count = 0U;
    size_t triggered = 0U;
    status_t r;

    (void)printf("Test 6: Alerts Test\n");

    /* NULL guards */
    r = alertsInit(NULL);
    reportCase("unit","alertsInit NULL=>INVALID_ARG",(r==STATUS_ERR_INVALID_ARG),"INVALID_ARG",status_to_string(r),&fails);
    r = alertsCreate(NULL,"AAPL",100.0,ALERT_ABOVE,"u");
    reportCase("unit","alertsCreate NULL store=>INVALID_ARG",(r==STATUS_ERR_INVALID_ARG),"INVALID_ARG",status_to_string(r),&fails);
    r = alertsCheckPrice(NULL,"AAPL",100.0,&triggered);
    reportCase("unit","alertsCheckPrice NULL store=>INVALID_ARG",(r==STATUS_ERR_INVALID_ARG),"INVALID_ARG",status_to_string(r),&fails);
    r = alertsGetAll(NULL,all,MAX_ALERTS,&count);
    reportCase("unit","alertsGetAll NULL store=>INVALID_ARG",(r==STATUS_ERR_INVALID_ARG),"INVALID_ARG",status_to_string(r),&fails);

    (void)alertsInit(&store);

    /* count on empty store */
    reportCase("unit","alertsCount empty==0",(alertsCount(&store)==0U),"0","ok",&fails);

    /* create ABOVE alert and fire it */
    r = alertsCreate(&store,"AAPL",150.0,ALERT_ABOVE,"testuser");
    reportCase("unit","alertsCreate ABOVE=>OK",(r==STATUS_OK),"OK",status_to_string(r),&fails);
    reportCase("unit","alertsCount after create==1",(alertsCount(&store)==1U),"1","ok",&fails);

    triggered = 0U;
    r = alertsCheckPrice(&store,"AAPL",140.0,&triggered);
    reportCase("unit","ABOVE alert does not fire below threshold",(r==STATUS_OK)&&(triggered==0U),"0",status_to_string(r),&fails);

    r = alertsCheckPrice(&store,"AAPL",160.0,&triggered);
    reportCase("unit","ABOVE alert fires at threshold cross",(r==STATUS_OK)&&(triggered==1U),"1",status_to_string(r),&fails);

    /* already triggered -> won't fire again */
    triggered = 0U;
    (void)alertsCheckPrice(&store,"AAPL",200.0,&triggered);
    reportCase("unit","Already-triggered alert not fired again",(triggered==0U),"0",(triggered==0U)?"ok":"re-fired",&fails);

    /* create BELOW alert */
    r = alertsCreate(&store,"MSFT",50.0,ALERT_BELOW,"admin");
    reportCase("unit","alertsCreate BELOW=>OK",(r==STATUS_OK),"OK",status_to_string(r),&fails);

    triggered = 0U;
    (void)alertsCheckPrice(&store,"MSFT",60.0,&triggered);
    reportCase("unit","BELOW alert does not fire above threshold",(triggered==0U),"0","ok",&fails);

    triggered = 0U;
    r = alertsCheckPrice(&store,"MSFT",40.0,&triggered);
    reportCase("unit","BELOW alert fires at threshold cross",(r==STATUS_OK)&&(triggered==1U),"1",status_to_string(r),&fails);

    /* exact threshold fires (>= and <=) */
    r = alertsCreate(&store,"TSLA",100.0,ALERT_ABOVE,"u");
    reportCase("unit","alertsCreate TSLA ABOVE=>OK",(r==STATUS_OK),"OK",status_to_string(r),&fails);
    triggered = 0U;
    (void)alertsCheckPrice(&store,"TSLA",100.0,&triggered);
    reportCase("unit","ABOVE fires at exact threshold",(triggered==1U),"1","ok",&fails);

    r = alertsCreate(&store,"AMZN",100.0,ALERT_BELOW,"u");
    reportCase("unit","alertsCreate AMZN BELOW=>OK",(r==STATUS_OK),"OK",status_to_string(r),&fails);
    triggered = 0U;
    (void)alertsCheckPrice(&store,"AMZN",100.0,&triggered);
    reportCase("unit","BELOW fires at exact threshold",(triggered==1U),"1","ok",&fails);

    /* getAll */
    r = alertsGetAll(&store,all,MAX_ALERTS,&count);
    reportCase("unit","alertsGetAll=>OK",(r==STATUS_OK),"OK",status_to_string(r),&fails);
    reportCase("unit","alertsGetAll returns correct count",(count==4U),"4","ok",&fails);

    /* outTriggeredCount NULL is safe */
    (void)alertsCreate(&store,"NVDA",200.0,ALERT_ABOVE,"u");
    r = alertsCheckPrice(&store,"NVDA",250.0,NULL);
    reportCase("unit","alertsCheckPrice NULL outCount=>OK",(r==STATUS_OK),"OK",status_to_string(r),&fails);

    /* save/load round-trip */
    {
        AlertStore store2;
        size_t cnt2 = 0U;
        r = alertsSaveToPath(&store,"data/.tester_alerts.db");
        reportCase("unit","alertsSaveToPath=>OK",(r==STATUS_OK),"OK",status_to_string(r),&fails);
        (void)alertsInit(&store2);
        r = alertsLoadFromPath(&store2,"data/.tester_alerts.db");
        reportCase("unit","alertsLoadFromPath=>OK",(r==STATUS_OK),"OK",status_to_string(r),&fails);
        (void)alertsGetAll(&store2,all,MAX_ALERTS,&cnt2);
        reportCase("unit","Alerts reload same count",(cnt2==count+1U),"5","ok",&fails);
        (void)alertsDestroy(&store2);
        (void)remove("data/.tester_alerts.db");
    }

    /* bad paths */
    r = alertsSaveToPath(&store,"/no/such/path/a.db");
    reportCase("unit","alertsSaveToPath bad path=>IO_ERROR",(r==STATUS_ERR_IO),"IO_ERROR",status_to_string(r),&fails);
    r = alertsLoadFromPath(&store,"/no/such/path/a.db");
    reportCase("unit","alertsLoadFromPath bad path=>IO_ERROR",(r==STATUS_ERR_IO),"IO_ERROR",status_to_string(r),&fails);

    (void)alertsDestroy(&store);
    r = alertsDestroy(NULL);
    reportCase("unit","alertsDestroy NULL=>INVALID_ARG",(r==STATUS_ERR_INVALID_ARG),"INVALID_ARG",status_to_string(r),&fails);
    return fails;
}

/* ===================== UNIT TEST 7: Auth ================================ */
static int unitTest7_Auth(void)
{
    int fails = 0;
    UserStore store;
    User outUser;
    status_t r;

    (void)printf("Test 7: Auth Test\n");

    r = authInit(NULL);
    reportCase("unit","authInit NULL=>INVALID_ARG",(r==STATUS_ERR_INVALID_ARG),"INVALID_ARG",status_to_string(r),&fails);
    (void)authInit(&store);

    /* load from scratch file with known users */
    r = authCreateDefaultUsers("data/.tester_users.txt");
    reportCase("unit","authCreateDefaultUsers=>OK",(r==STATUS_OK),"OK",status_to_string(r),&fails);
    r = authLoadUsersFromPath(&store,"data/.tester_users.txt");
    reportCase("unit","authLoadUsersFromPath=>OK",(r==STATUS_OK),"OK",status_to_string(r),&fails);
    reportCase("unit","Users loaded (>0)",(store.count>0U),"true","ok",&fails);

    /* successful login */
    r = authLogin(&store,"admin","admin123",&outUser);
    reportCase("unit","authLogin admin=>OK",(r==STATUS_OK),"OK",status_to_string(r),&fails);
    reportCase("unit","authIsAdmin admin=>true",authIsAdmin(&outUser),"true","ok",&fails);
    reportCase("unit","authIsTester admin=>false",!authIsTester(&outUser),"false","ok",&fails);

    r = authLogin(&store,"tester","tester123",&outUser);
    reportCase("unit","authLogin tester=>OK",(r==STATUS_OK),"OK",status_to_string(r),&fails);
    reportCase("unit","authIsTester tester=>true",authIsTester(&outUser),"true","ok",&fails);
    reportCase("unit","authIsAdmin tester=>false",!authIsAdmin(&outUser),"false","ok",&fails);

    r = authLogin(&store,"user","user123",&outUser);
    reportCase("unit","authLogin user=>OK",(r==STATUS_OK),"OK",status_to_string(r),&fails);
    reportCase("unit","authIsAdmin user=>false",!authIsAdmin(&outUser),"false","ok",&fails);

    /* bad password */
    r = authLogin(&store,"admin","wrong",&outUser);
    reportCase("unit","authLogin bad password=>AUTH_FAILED",(r==STATUS_ERR_AUTH),"AUTH_FAILED",status_to_string(r),&fails);

    /* unknown user */
    r = authLogin(&store,"nobody","pass",&outUser);
    reportCase("unit","authLogin unknown user=>AUTH_FAILED",(r==STATUS_ERR_AUTH),"AUTH_FAILED",status_to_string(r),&fails);

    /* NULL args */
    r = authLogin(NULL,"admin","admin123",&outUser);
    reportCase("unit","authLogin NULL store=>INVALID_ARG",(r==STATUS_ERR_INVALID_ARG),"INVALID_ARG",status_to_string(r),&fails);
    r = authLogin(&store,NULL,"admin123",&outUser);
    reportCase("unit","authLogin NULL user=>INVALID_ARG",(r==STATUS_ERR_INVALID_ARG),"INVALID_ARG",status_to_string(r),&fails);

    /* authIsAdmin/IsTester NULL */
    reportCase("unit","authIsAdmin NULL=>false",!authIsAdmin(NULL),"false","ok",&fails);
    reportCase("unit","authIsTester NULL=>false",!authIsTester(NULL),"false","ok",&fails);

    /* bad path */
    r = authLoadUsersFromPath(NULL,"data/.tester_users.txt");
    reportCase("unit","authLoadUsersFromPath NULL store=>INVALID_ARG",(r==STATUS_ERR_INVALID_ARG),"INVALID_ARG",status_to_string(r),&fails);

    (void)remove("data/.tester_users.txt");
    return fails;
}

/* ===================== UNIT TEST 8: Common helpers ====================== */
static int unitTest8_CommonHelpers(void)
{
    int fails = 0;
    char buf[16];
    status_t r;

    (void)printf("Test 8: Common Helpers Test\n");

    /* safe_strcpy normal */
    r = safe_strcpy(buf,sizeof(buf),"Hello");
    reportCase("unit","safe_strcpy normal=>OK",(r==STATUS_OK),"OK",status_to_string(r),&fails);
    reportCase("unit","safe_strcpy copies correctly",(buf[0]=='H'),"H","ok",&fails);

    /* safe_strcpy NULL dst */
    r = safe_strcpy(NULL,sizeof(buf),"Hello");
    reportCase("unit","safe_strcpy NULL dst=>INVALID_ARG",(r==STATUS_ERR_INVALID_ARG),"INVALID_ARG",status_to_string(r),&fails);

    /* safe_strcpy NULL src */
    r = safe_strcpy(buf,sizeof(buf),NULL);
    reportCase("unit","safe_strcpy NULL src=>INVALID_ARG",(r==STATUS_ERR_INVALID_ARG),"INVALID_ARG",status_to_string(r),&fails);

    /* safe_strcpy zero dstSize */
    r = safe_strcpy(buf,0U,"Hello");
    reportCase("unit","safe_strcpy dstSize==0=>INVALID_ARG",(r==STATUS_ERR_INVALID_ARG),"INVALID_ARG",status_to_string(r),&fails);

    /* truncation: src longer than dst */
    r = safe_strcpy(buf,4U,"Hello");
    reportCase("unit","safe_strcpy truncates and NUL-terminates",(r==STATUS_OK)&&(buf[3]=='\0'),"NUL at [3]","ok",&fails);

    /* safe_strcasecmp equal strings */
    reportCase("unit","safe_strcasecmp equal=>0",(safe_strcasecmp("AAPL","aapl")==0),"0","ok",&fails);
    reportCase("unit","safe_strcasecmp different<0",(safe_strcasecmp("AAPL","MSFT")!=0),"nonzero","ok",&fails);
    reportCase("unit","safe_strcasecmp NULL,NULL=>0",(safe_strcasecmp(NULL,NULL)==0),"0","ok",&fails);
    reportCase("unit","safe_strcasecmp NULL,str!=0",(safe_strcasecmp(NULL,"X")!=0),"nonzero","ok",&fails);

    /* portable_strnlen */
    reportCase("unit","portable_strnlen normal",(portable_strnlen("Hello",10)==5U),"5","ok",&fails);
    reportCase("unit","portable_strnlen NULL=>0",(portable_strnlen(NULL,10)==0U),"0","ok",&fails);
    reportCase("unit","portable_strnlen maxLen cap",(portable_strnlen("Hello",3)==3U),"3","ok",&fails);
    reportCase("unit","portable_strnlen empty=>0",(portable_strnlen("",10)==0U),"0","ok",&fails);

    /* status_to_string coverage */
    reportCase("unit","status_to_string OK",(status_to_string(STATUS_OK)!=NULL),"non-null","ok",&fails);
    reportCase("unit","status_to_string NOT_FOUND",(status_to_string(STATUS_ERR_NOT_FOUND)!=NULL),"non-null","ok",&fails);
    reportCase("unit","status_to_string DUPLICATE",(status_to_string(STATUS_ERR_DUPLICATE)!=NULL),"non-null","ok",&fails);
    reportCase("unit","status_to_string MEMORY",(status_to_string(STATUS_ERR_MEMORY)!=NULL),"non-null","ok",&fails);
    reportCase("unit","status_to_string AUTH",(status_to_string(STATUS_ERR_AUTH)!=NULL),"non-null","ok",&fails);
    reportCase("unit","status_to_string PERMISSION",(status_to_string(STATUS_ERR_PERMISSION)!=NULL),"non-null","ok",&fails);
    reportCase("unit","status_to_string LOCK",(status_to_string(STATUS_ERR_LOCK)!=NULL),"non-null","ok",&fails);
    reportCase("unit","status_to_string FULL",(status_to_string(STATUS_ERR_FULL)!=NULL),"non-null","ok",&fails);
    reportCase("unit","status_to_string IO",(status_to_string(STATUS_ERR_IO)!=NULL),"non-null","ok",&fails);

    /* location_status_to_string */
    reportCase("unit","loc_to_str NONE",(location_status_to_string(LOC_NONE)!=NULL),"non-null","ok",&fails);
    reportCase("unit","loc_to_str MAIN_ONLY",(location_status_to_string(LOC_MAIN_ONLY)!=NULL),"non-null","ok",&fails);
    reportCase("unit","loc_to_str CACHE_ONLY",(location_status_to_string(LOC_CACHE_ONLY)!=NULL),"non-null","ok",&fails);
    reportCase("unit","loc_to_str BOTH",(location_status_to_string(LOC_BOTH)!=NULL),"non-null","ok",&fails);

    return fails;
}

/* ===================== UNIT TEST 9: Query =============================== */
static int unitTest9_Query(void)
{
    int fails = 0;
    MainCache mc;
    SearchCache sc;
    Stats stats;
    Stock outStock;
    location_status_t loc;
    status_t r;

    (void)printf("Test 9: Query Test\n");

    (void)mainCacheInit(&mc);
    (void)searchCacheInit(&sc);
    (void)statsInit(&stats);

    /* add a stock to main only */
    {
        Stock st;
        memset(&st,0,sizeof(st));
        (void)safe_strcpy(st.symbol,sizeof(st.symbol),"QRYA");
        (void)safe_strcpy(st.name,sizeof(st.name),"Query A");
        st.price = 55.0;
        (void)mainCacheAdd(&mc,&st);
    }

    /* queryLocationStatus: LOC_MAIN_ONLY */
    loc = queryLocationStatus(&mc,&sc,"QRYA");
    reportCase("unit","queryLocationStatus MAIN_ONLY",(loc==LOC_MAIN_ONLY),"MAIN_ONLY",location_status_to_string(loc),&fails);

    /* queryLocationStatus: LOC_NONE */
    loc = queryLocationStatus(&mc,&sc,"UNKN");
    reportCase("unit","queryLocationStatus NONE",(loc==LOC_NONE),"NONE",location_status_to_string(loc),&fails);

    /* queryExecuteSearch NULL arg */
    r = queryExecuteSearch(NULL,&sc,&stats,"QRYA",&outStock,&loc);
    reportCase("unit","queryExecuteSearch NULL mainDb=>INVALID_ARG",(r==STATUS_ERR_INVALID_ARG),"INVALID_ARG",status_to_string(r),&fails);

    /* queryExecuteSearch invalid symbol */
    r = queryExecuteSearch(&mc,&sc,&stats,"@BAD",&outStock,&loc);
    reportCase("unit","queryExecuteSearch bad symbol=>INVALID_ARG",(r==STATUS_ERR_INVALID_ARG),"INVALID_ARG",status_to_string(r),&fails);

    /* queryExecuteSearch not found */
    r = queryExecuteSearch(&mc,&sc,&stats,"XXXX",&outStock,&loc);
    reportCase("unit","queryExecuteSearch not found=>NOT_FOUND",(r==STATUS_ERR_NOT_FOUND),"NOT_FOUND",status_to_string(r),&fails);
    reportCase("unit","queryExecuteSearch not found loc==NONE",(loc==LOC_NONE),"NONE",location_status_to_string(loc),&fails);

    /* queryExecuteSearch main-DB hit (cache miss) */
    r = queryExecuteSearch(&mc,&sc,&stats,"QRYA",&outStock,&loc);
    reportCase("unit","queryExecuteSearch main-DB hit=>OK",(r==STATUS_OK),"OK",status_to_string(r),&fails);
    reportCase("unit","queryExecuteSearch loc==MAIN_ONLY",(loc==LOC_MAIN_ONLY),"MAIN_ONLY",location_status_to_string(loc),&fails);
    reportCase("unit","queryExecuteSearch now in search cache",searchCacheContains(&sc,"QRYA"),"true","ok",&fails);

    /* queryLocationStatus: LOC_BOTH after search populates cache */
    loc = queryLocationStatus(&mc,&sc,"QRYA");
    reportCase("unit","queryLocationStatus BOTH after search",(loc==LOC_BOTH),"BOTH",location_status_to_string(loc),&fails);

    /* second search = cache HIT */
    r = queryExecuteSearch(&mc,&sc,&stats,"QRYA",&outStock,&loc);
    reportCase("unit","queryExecuteSearch cache HIT=>OK",(r==STATUS_OK),"OK",status_to_string(r),&fails);
    reportCase("unit","cache HIT loc==BOTH",(loc==LOC_BOTH),"BOTH",location_status_to_string(loc),&fails);

    /* outLocation NULL is safe */
    r = queryExecuteSearch(&mc,&sc,&stats,"QRYA",&outStock,NULL);
    reportCase("unit","queryExecuteSearch NULL loc=>OK",(r==STATUS_OK),"OK",status_to_string(r),&fails);

    (void)statsDestroy(&stats);
    (void)searchCacheDestroy(&sc);
    (void)mainCacheDestroy(&mc);
    return fails;
}

/* ===================== UNIT TEST 10: Feed =============================== */
static int unitTest10_Feed(void)
{
    int fails = 0;
    MainCache mc;
    SearchCache sc;
    AlertStore alerts;
    Stats stats;
    status_t r;

    (void)printf("Test 10: Feed Test\n");

    (void)mainCacheInit(&mc);
    (void)searchCacheInit(&sc);
    (void)alertsInit(&alerts);
    (void)statsInit(&stats);

    /* NULL arg guards */
    r = feedApplyPriceUpdate(NULL,&sc,&alerts,&stats,"AAPL",100.0);
    reportCase("unit","feedApply NULL mainDb=>INVALID_ARG",(r==STATUS_ERR_INVALID_ARG),"INVALID_ARG",status_to_string(r),&fails);
    r = feedApplyPriceUpdate(&mc,NULL,&alerts,&stats,"AAPL",100.0);
    reportCase("unit","feedApply NULL searchDb=>INVALID_ARG",(r==STATUS_ERR_INVALID_ARG),"INVALID_ARG",status_to_string(r),&fails);
    r = feedApplyPriceUpdate(&mc,&sc,NULL,&stats,"AAPL",100.0);
    reportCase("unit","feedApply NULL alerts=>INVALID_ARG",(r==STATUS_ERR_INVALID_ARG),"INVALID_ARG",status_to_string(r),&fails);
    r = feedApplyPriceUpdate(&mc,&sc,&alerts,NULL,"AAPL",100.0);
    reportCase("unit","feedApply NULL stats=>INVALID_ARG",(r==STATUS_ERR_INVALID_ARG),"INVALID_ARG",status_to_string(r),&fails);
    r = feedApplyPriceUpdate(&mc,&sc,&alerts,&stats,NULL,100.0);
    reportCase("unit","feedApply NULL symbol=>INVALID_ARG",(r==STATUS_ERR_INVALID_ARG),"INVALID_ARG",status_to_string(r),&fails);

    /* invalid symbol format */
    r = feedApplyPriceUpdate(&mc,&sc,&alerts,&stats,"@BAD",100.0);
    reportCase("unit","feedApply bad symbol=>INVALID_ARG",(r==STATUS_ERR_INVALID_ARG),"INVALID_ARG",status_to_string(r),&fails);

    /* invalid price */
    r = feedApplyPriceUpdate(&mc,&sc,&alerts,&stats,"AAPL",-5.0);
    reportCase("unit","feedApply negative price=>INVALID_ARG",(r==STATUS_ERR_INVALID_ARG),"INVALID_ARG",status_to_string(r),&fails);

    /* symbol not in main DB => NOT_FOUND */
    r = feedApplyPriceUpdate(&mc,&sc,&alerts,&stats,"AAPL",100.0);
    reportCase("unit","feedApply unknown symbol=>NOT_FOUND",(r==STATUS_ERR_NOT_FOUND),"NOT_FOUND",status_to_string(r),&fails);

    /* add stock and update successfully */
    {
        Stock st;
        memset(&st,0,sizeof(st));
        (void)safe_strcpy(st.symbol,sizeof(st.symbol),"FEED");
        (void)safe_strcpy(st.name,sizeof(st.name),"Feed Test Co");
        st.price = 100.0;
        (void)mainCacheAdd(&mc,&st);
    }
    r = feedApplyPriceUpdate(&mc,&sc,&alerts,&stats,"FEED",120.0);
    reportCase("unit","feedApply valid update=>OK",(r==STATUS_OK),"OK",status_to_string(r),&fails);
    {
        Stock found;
        (void)mainCacheSearch(&mc,"FEED",&found);
        reportCase("unit","feedApply price reflected in mainDb",(found.price==120.0),"120.0",(found.price==120.0)?"ok":"mismatch",&fails);
    }

    /* update when symbol is also in search cache (sync rule) */
    (void)searchCacheContains(&sc,"FEED"); /* put it in cache first */
    {
        Stock touchSt;
        memset(&touchSt,0,sizeof(touchSt));
        (void)safe_strcpy(touchSt.symbol,sizeof(touchSt.symbol),"FEED");
        (void)safe_strcpy(touchSt.name,sizeof(touchSt.name),"Feed Test Co");
        touchSt.price = 120.0;
        (void)searchCacheTouch(&sc,&touchSt);
    }
    r = feedApplyPriceUpdate(&mc,&sc,&alerts,&stats,"FEED",150.0);
    reportCase("unit","feedApply sync rule=>OK",(r==STATUS_OK),"OK",status_to_string(r),&fails);
    {
        Stock found;
        (void)searchCacheSearch(&sc,"FEED",&found);
        reportCase("unit","feedApply synced search cache price",(found.price==150.0),"150.0",(found.price==150.0)?"ok":"mismatch",&fails);
    }

    /* feedSimulateTick NULL guards */
    {
        char sym[SYMBOL_MAX_LEN]; double p;
        r = feedSimulateTick(NULL,&sc,&alerts,&stats,sym,sizeof(sym),&p);
        reportCase("unit","feedSimulateTick NULL mainDb=>INVALID_ARG",(r==STATUS_ERR_INVALID_ARG),"INVALID_ARG",status_to_string(r),&fails);
    }

    /* feedSimulateTick on non-empty DB */
    {
        char sym[SYMBOL_MAX_LEN]; double p = 0.0;
        r = feedSimulateTick(&mc,&sc,&alerts,&stats,sym,sizeof(sym),&p);
        reportCase("unit","feedSimulateTick=>OK",(r==STATUS_OK),"OK",status_to_string(r),&fails);
        reportCase("unit","feedSimulateTick returned positive price",(p>0.0),"positive","ok",&fails);
    }

    /* feedSimulateTick on empty DB => NOT_FOUND */
    {
        MainCache emptyMc; char sym[SYMBOL_MAX_LEN]; double p;
        (void)mainCacheInit(&emptyMc);
        r = feedSimulateTick(&emptyMc,&sc,&alerts,&stats,sym,sizeof(sym),&p);
        reportCase("unit","feedSimulateTick empty DB=>NOT_FOUND",(r==STATUS_ERR_NOT_FOUND),"NOT_FOUND",status_to_string(r),&fails);
        (void)mainCacheDestroy(&emptyMc);
    }

    (void)statsDestroy(&stats);
    (void)alertsDestroy(&alerts);
    (void)searchCacheDestroy(&sc);
    (void)mainCacheDestroy(&mc);
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
    totalFails += unitTest6_Alerts();
    totalFails += unitTest7_Auth();
    totalFails += unitTest8_CommonHelpers();
    totalFails += unitTest9_Query();
    totalFails += unitTest10_Feed();
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
