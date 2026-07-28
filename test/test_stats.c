/*
 * test_stats.c — CUnit tests for Stats counters and hit-ratio math
 * Mirrors: unitTest5_Statistics() in src/testing.c
 *
 * ── Compile & run (from the test/ directory) ──────────────────────────
 *   gcc -std=c11 -Wall -Wextra -I../include -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
 *       test_stats.c \
 *       ../src/stats.c ../src/common.c \
 *       -lcunit -lpthread -o test_stats
 *   ./test_stats
 *
 * ── Headers used ──────────────────────────────────────────────────────
 *   stats.h  — Stats, StatsSnapshot, statsInit/Destroy, RecordSearch/Update/HitRatio
 *   common.h — status_t, …
 *
 * ── Sources compiled ──────────────────────────────────────────────────
 *   ../src/stats.c  — all Stats* functions
 *   ../src/common.c — status_to_string(), …
 */

#include "common.h"
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "stats.h"

static Stats g_stats;

static int suite_setup(void)
{
    return (statsInit(&g_stats) == STATUS_OK) ? 0 : 1;
}

static int suite_teardown(void)
{
    (void)statsDestroy(&g_stats);
    return 0;
}

/* ── Test cases ──────────────────────────────────────────────────────── */

static void test_initial_counters_are_zero(void)
{
    Stats fresh;
    StatsSnapshot snap;
    (void)statsInit(&fresh);
    (void)statsGetSnapshot(&fresh, &snap);
    CU_ASSERT_EQUAL(snap.searchCount, 0UL);
    CU_ASSERT_EQUAL(snap.updateCount, 0UL);
    CU_ASSERT_EQUAL(snap.cacheHits,   0UL);
    CU_ASSERT_EQUAL(snap.cacheMisses, 0UL);
    CU_ASSERT_DOUBLE_EQUAL(snap.hitRatio, 0.0, 0.0001);
    (void)statsDestroy(&fresh);
}

static void test_record_search_increments_count(void)
{
    StatsSnapshot snap;
    statsRecordSearch(&g_stats);
    statsRecordSearch(&g_stats);
    (void)statsGetSnapshot(&g_stats, &snap);
    CU_ASSERT(snap.searchCount >= 2UL);
}

static void test_record_update_increments_count(void)
{
    StatsSnapshot snap;
    statsRecordUpdate(&g_stats);
    (void)statsGetSnapshot(&g_stats, &snap);
    CU_ASSERT(snap.updateCount >= 1UL);
}

static void test_hit_ratio_correct_3hits_1miss(void)
{
    Stats s;
    double ratio;

    (void)statsInit(&s);
    statsRecordCacheHit(&s);
    statsRecordCacheHit(&s);
    statsRecordCacheHit(&s);
    statsRecordCacheMiss(&s);
    /* 3 hits / (3+1) = 0.75 */
    ratio = statsGetHitRatio(&s);
    CU_ASSERT_DOUBLE_EQUAL(ratio, 0.75, 0.001);
    (void)statsDestroy(&s);
}

static void test_hit_ratio_zero_when_no_lookups(void)
{
    Stats s;
    (void)statsInit(&s);
    CU_ASSERT_DOUBLE_EQUAL(statsGetHitRatio(&s), 0.0, 0.0001);
    (void)statsDestroy(&s);
}

static void test_hit_ratio_1_when_all_hits(void)
{
    Stats s;
    (void)statsInit(&s);
    statsRecordCacheHit(&s);
    statsRecordCacheHit(&s);
    CU_ASSERT_DOUBLE_EQUAL(statsGetHitRatio(&s), 1.0, 0.0001);
    (void)statsDestroy(&s);
}

static void test_snapshot_counters_match_recorded_operations(void)
{
    Stats s;
    StatsSnapshot snap;

    (void)statsInit(&s);
    statsRecordSearch(&s);
    statsRecordSearch(&s);
    statsRecordCacheHit(&s);
    statsRecordCacheHit(&s);
    statsRecordCacheHit(&s);
    statsRecordCacheMiss(&s);
    statsRecordUpdate(&s);

    (void)statsGetSnapshot(&s, &snap);
    CU_ASSERT_EQUAL(snap.searchCount, 2UL);
    CU_ASSERT_EQUAL(snap.updateCount, 1UL);
    CU_ASSERT_EQUAL(snap.cacheHits,   3UL);
    CU_ASSERT_EQUAL(snap.cacheMisses, 1UL);
    CU_ASSERT_DOUBLE_EQUAL(snap.hitRatio, 0.75, 0.001);

    (void)statsDestroy(&s);
}

static void test_get_snapshot_null_returns_invalid_arg(void)
{
    status_t r = statsGetSnapshot(&g_stats, NULL);
    CU_ASSERT_EQUAL(r, STATUS_ERR_INVALID_ARG);
}

static void test_get_snapshot_null_stats_returns_invalid_arg(void)
{
    StatsSnapshot snap;
    status_t r = statsGetSnapshot(NULL, &snap);
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

    suite = CU_add_suite("Statistics", suite_setup, suite_teardown);
    if (suite == NULL)
    {
        CU_cleanup_registry();
        return (int)CU_get_error();
    }

    if ((CU_add_test(suite, "Initial counters are all zero",                      test_initial_counters_are_zero)             == NULL) ||
        (CU_add_test(suite, "RecordSearch increments searchCount",                test_record_search_increments_count)        == NULL) ||
        (CU_add_test(suite, "RecordUpdate increments updateCount",                test_record_update_increments_count)        == NULL) ||
        (CU_add_test(suite, "Hit ratio correct: 3 hits / 1 miss = 0.75",          test_hit_ratio_correct_3hits_1miss)         == NULL) ||
        (CU_add_test(suite, "Hit ratio is 0.0 when no lookups recorded",          test_hit_ratio_zero_when_no_lookups)        == NULL) ||
        (CU_add_test(suite, "Hit ratio is 1.0 when all lookups are hits",         test_hit_ratio_1_when_all_hits)             == NULL) ||
        (CU_add_test(suite, "Snapshot counters match recorded operations exactly",test_snapshot_counters_match_recorded_operations) == NULL) ||
        (CU_add_test(suite, "GetSnapshot with NULL outSnap returns INVALID_ARG",  test_get_snapshot_null_returns_invalid_arg) == NULL) ||
        (CU_add_test(suite, "GetSnapshot with NULL stats returns INVALID_ARG",    test_get_snapshot_null_stats_returns_invalid_arg) == NULL))
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
