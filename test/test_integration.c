/*
 * test_integration.c — CUnit integration tests: full data-flow pipeline
 * Mirrors: testerRunIntegrationTests() in src/testing.c
 *
 * ── Compile & run (from the test/ directory) ──────────────────────────
 *   gcc -std=c11 -Wall -Wextra -I../include -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
 *       test_integration.c \
 *       ../src/cache.c ../src/searchcache.c ../src/alerts.c \
 *       ../src/stats.c ../src/query.c ../src/feed.c \
 *       ../src/security.c ../src/logger.c \
 *       ../src/common.c ../src/memory.c \
 *       -lcunit -lpthread -o test_integration
 *   ./test_integration
 *
 * ── Headers used ──────────────────────────────────────────────────────
 *   cache.h       — MainCache : mainCacheInit/Add/Search/Destroy
 *   searchcache.h — SearchCache: searchCacheInit/Contains/Search/Destroy
 *   alerts.h      — AlertStore : alertsInit/Create/CheckPrice/GetAll/Destroy
 *   stats.h       — Stats      : statsInit/Destroy
 *   query.h       — queryExecuteSearch()
 *   feed.h        — feedApplyPriceUpdate()
 *   logger.h      — loggerInit(), loggerLog(), loggerShutdown()
 *   common.h      — status_t, Stock, safe_strcpy(), …
 *   memory.h      — mmInit(), mmShutdown()
 *
 * ── Sources compiled ──────────────────────────────────────────────────
 *   ../src/cache.c       — MainCache implementation
 *   ../src/searchcache.c — SearchCache / LRU implementation
 *   ../src/alerts.c      — AlertStore implementation
 *   ../src/stats.c       — Stats counters and hit-ratio
 *   ../src/query.c       — queryExecuteSearch() (ties cache + main DB + stats)
 *   ../src/feed.c        — feedApplyPriceUpdate() (updates DB + cache + alerts)
 *   ../src/security.c    — secValidateSymbol/Price (used by feed)
 *   ../src/logger.c      — loggerLog() (called by query, feed, alerts)
 *   ../src/common.c      — safe_strcpy(), safe_strcasecmp(), …
 *   ../src/memory.c      — mmAlloc(), mmFree()
 */

#include "common.h"
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <string.h>
#include "cache.h"
#include "searchcache.h"
#include "alerts.h"
#include "stats.h"
#include "query.h"
#include "feed.h"
#include "logger.h"
#include "memory.h"
#include <sys/stat.h>

/* ── Shared integration state (owned by suite_setup / suite_teardown) ── */

static MainCache  g_mc;
static SearchCache g_sc;
static AlertStore  g_alerts;
static Stats       g_stats;

static int suite_setup(void)
{
    int ok = 1;
    (void)mkdir("logs", 0777);
    (void)mmInit();
    (void)loggerInit();
    ok &= (mainCacheInit(&g_mc)       == STATUS_OK);
    ok &= (searchCacheInit(&g_sc)     == STATUS_OK);
    ok &= (alertsInit(&g_alerts)      == STATUS_OK);
    ok &= (statsInit(&g_stats)        == STATUS_OK);
    return ok ? 0 : 1;
}

static int suite_teardown(void)
{
    (void)mainCacheDestroy(&g_mc);
    (void)searchCacheDestroy(&g_sc);
    (void)alertsDestroy(&g_alerts);
    (void)statsDestroy(&g_stats);
    (void)loggerShutdown();
    (void)mmShutdown();
    return 0;
}

/* ── Step 1: Seed the main database ─────────────────────────────────── */

static void test_step1_add_stock_to_main_db(void)
{
    Stock st;
    memset(&st, 0, sizeof(st));
    (void)safe_strcpy(st.symbol, sizeof(st.symbol), "ITGX");
    (void)safe_strcpy(st.name,   sizeof(st.name),   "Integration Test Co");
    st.price = 100.0;

    status_t r = mainCacheAdd(&g_mc, &st);
    CU_ASSERT_EQUAL(r, STATUS_OK);
}

/* ── Step 2: Search must hit main DB and populate the search cache ───── */

static void test_step2_search_populates_search_cache(void)
{
    Stock found;
    location_status_t loc;
    status_t r = queryExecuteSearch(&g_mc, &g_sc, &g_stats, "ITGX", &found, &loc);
    CU_ASSERT_EQUAL(r, STATUS_OK);
    CU_ASSERT_TRUE(searchCacheContains(&g_sc, "ITGX"));
    CU_ASSERT_EQUAL(loc, LOC_MAIN_ONLY); /* first search: main DB hit, not yet in cache */
}

/* Second search must be a cache hit */
static void test_step2b_second_search_is_cache_hit(void)
{
    Stock found;
    location_status_t loc;
    status_t r = queryExecuteSearch(&g_mc, &g_sc, &g_stats, "ITGX", &found, &loc);
    CU_ASSERT_EQUAL(r, STATUS_OK);
    CU_ASSERT_EQUAL(loc, LOC_BOTH);
}

/* ── Step 3: Create a price alert ────────────────────────────────────── */

static void test_step3_create_above_alert(void)
{
    status_t r = alertsCreate(&g_alerts, "ITGX", 150.0, ALERT_ABOVE, "tester");
    CU_ASSERT_EQUAL(r, STATUS_OK);
    CU_ASSERT_EQUAL(alertsCount(&g_alerts), 1U);
}

/* ── Step 4: Feed update must update main DB, sync cache, fire alert ─── */

static void test_step4_feed_update_succeeds(void)
{
    status_t r = feedApplyPriceUpdate(&g_mc, &g_sc, &g_alerts, &g_stats, "ITGX", 160.0);
    CU_ASSERT_EQUAL(r, STATUS_OK);
}

/* ── Step 5: Main DB must reflect the new price ──────────────────────── */

static void test_step5_main_db_reflects_new_price(void)
{
    Stock found;
    (void)mainCacheSearch(&g_mc, "ITGX", &found);
    CU_ASSERT_DOUBLE_EQUAL(found.price, 160.0, 0.001);
}

/* ── Step 6: Search cache must also reflect the new price (sync rule) ── */

static void test_step6_search_cache_synced_to_new_price(void)
{
    Stock found;
    (void)searchCacheSearch(&g_sc, "ITGX", &found);  /* direct cache lookup */
    CU_ASSERT_DOUBLE_EQUAL(found.price, 160.0, 0.001);
}

/* ── Step 7: The ABOVE 150 alert must have been triggered ─────────────── */

static void test_step7_alert_triggered_by_price_update(void)
{
    Alert all[MAX_ALERTS];
    size_t count = 0U;
    size_t i;
    bool wasTriggered = false;

    (void)alertsGetAll(&g_alerts, all, MAX_ALERTS, &count);
    for (i = 0U; i < count; i++)
    {
        if ((safe_strcasecmp(all[i].symbol, "ITGX") == 0) && all[i].triggered)
        {
            wasTriggered = true;
        }
    }
    CU_ASSERT_TRUE(wasTriggered);
}

/* ── Step 8: Logger must accept an entry without error ───────────────── */

static void test_step8_logger_write_succeeds(void)
{
    status_t r = loggerLog(LOG_HISTORY, "Integration test marker entry");
    CU_ASSERT_EQUAL(r, STATUS_OK);
}

/* ── Step 9: Stats counters must have been updated through the pipeline ─ */

static void test_step9_stats_recorded_correctly(void)
{
    StatsSnapshot snap;
    (void)statsGetSnapshot(&g_stats, &snap);
    /* Two searches (step 2 + 2b) and one feed update (step 4) */
    CU_ASSERT(snap.searchCount >= 2UL);
    CU_ASSERT(snap.updateCount >= 1UL);
    /* First search was a miss; second was a hit */
    CU_ASSERT(snap.cacheHits   >= 1UL);
    CU_ASSERT(snap.cacheMisses >= 1UL);
}

/* ── Step 10: Feed update on unknown symbol must fail gracefully ──────── */

static void test_step10_feed_update_unknown_symbol_fails(void)
{
    status_t r = feedApplyPriceUpdate(&g_mc, &g_sc, &g_alerts, &g_stats, "XXXX", 50.0);
    CU_ASSERT_NOT_EQUAL(r, STATUS_OK);
}

/* ── Step 11: Feed update with invalid price must be rejected ─────────── */

static void test_step11_feed_update_invalid_price_rejected(void)
{
    status_t r = feedApplyPriceUpdate(&g_mc, &g_sc, &g_alerts, &g_stats, "ITGX", -10.0);
    CU_ASSERT_EQUAL(r, STATUS_ERR_INVALID_ARG);
}

/* ── main ────────────────────────────────────────────────────────────── */

int main(void)
{
    CU_pSuite suite = NULL;
    unsigned int failures;

    if (CU_initialize_registry() != CUE_SUCCESS)
    {
        return (int)CU_get_error();
    }

    suite = CU_add_suite("Integration_Pipeline", suite_setup, suite_teardown);
    if (suite == NULL)
    {
        CU_cleanup_registry();
        return (int)CU_get_error();
    }

    /* Tests MUST run in declaration order — each step builds on the last */
    if ((CU_add_test(suite, "Step 1:  Add stock ITGX to main DB",                   test_step1_add_stock_to_main_db)            == NULL) ||
        (CU_add_test(suite, "Step 2:  Search ITGX populates search cache",          test_step2_search_populates_search_cache)   == NULL) ||
        (CU_add_test(suite, "Step 2b: Second search for ITGX is a cache hit",       test_step2b_second_search_is_cache_hit)     == NULL) ||
        (CU_add_test(suite, "Step 3:  Create ABOVE-150 alert for ITGX",             test_step3_create_above_alert)              == NULL) ||
        (CU_add_test(suite, "Step 4:  Feed update to 160.0 returns STATUS_OK",      test_step4_feed_update_succeeds)            == NULL) ||
        (CU_add_test(suite, "Step 5:  Main DB reflects new price (160.0)",          test_step5_main_db_reflects_new_price)      == NULL) ||
        (CU_add_test(suite, "Step 6:  Search cache synced to new price (160.0)",    test_step6_search_cache_synced_to_new_price)== NULL) ||
        (CU_add_test(suite, "Step 7:  ABOVE-150 alert is marked triggered",         test_step7_alert_triggered_by_price_update) == NULL) ||
        (CU_add_test(suite, "Step 8:  Logger accepts history entry without error",  test_step8_logger_write_succeeds)           == NULL) ||
        (CU_add_test(suite, "Step 9:  Stats counters updated through pipeline",     test_step9_stats_recorded_correctly)        == NULL) ||
        (CU_add_test(suite, "Step 10: Feed update for unknown symbol fails",        test_step10_feed_update_unknown_symbol_fails) == NULL) ||
        (CU_add_test(suite, "Step 11: Feed update with invalid price rejected",     test_step11_feed_update_invalid_price_rejected) == NULL))
    {
        CU_cleanup_registry();
        return (int)CU_get_error();
    }

    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();

    failures = CU_get_number_of_failures();
    CU_cleanup_registry();
    return (int)failures;
}
