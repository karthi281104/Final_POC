/*
 * test_persistence.c — CUnit tests for disk save/load round-trips
 * Mirrors: unitTest4_Persistence() in src/testing.c
 *
 * ── Compile & run (from the test/ directory) ──────────────────────────
 *   gcc -std=c11 -Wall -Wextra -I../include -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
 *       test_persistence.c \
 *       ../src/persistence.c ../src/cache.c ../src/searchcache.c \
 *       ../src/common.c ../src/memory.c ../src/logger.c \
 *       -lcunit -lpthread -o test_persistence
 *   ./test_persistence
 *
 * ── Headers used ──────────────────────────────────────────────────────
 *   persistence.h — saveMainDbToPath(), loadMainDbFromPath(),
 *                   saveCacheToPath(), loadCacheFromPath()
 *   cache.h       — MainCache, mainCacheInit/Add/Search/Destroy
 *   searchcache.h — SearchCache, searchCacheInit/Touch/Contains/Destroy
 *   common.h      — status_t, Stock, safe_strcpy(), …
 *   memory.h      — mmInit(), mmShutdown()
 *   logger.h      — loggerInit() (persistence.c writes audit logs)
 *
 * ── Sources compiled ──────────────────────────────────────────────────
 *   ../src/persistence.c  — saveMainDbToPath(), loadMainDbFromPath(), …
 *   ../src/cache.c        — MainCache implementation
 *   ../src/searchcache.c  — SearchCache / LRU implementation
 *   ../src/common.c       — safe_strcpy(), safe_strcasecmp(), …
 *   ../src/memory.c       — mmAlloc(), mmFree()
 *   ../src/logger.c       — loggerLog() (called by persistence seeder)
 */

#include "common.h"
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <string.h>
#include <stdio.h>
#include "persistence.h"
#include "cache.h"
#include "searchcache.h"
#include "memory.h"
#include "logger.h"

/* Scratch files written to /tmp so no directory creation is needed */
#define SCRATCH_STOCK_PATH "/tmp/cunit_test_stock.db"
#define SCRATCH_CACHE_PATH "/tmp/cunit_test_cache.db"

static int suite_setup(void)
{
    (void)mmInit();
    (void)loggerInit();
    return 0;
}

static int suite_teardown(void)
{
    (void)remove(SCRATCH_STOCK_PATH);
    (void)remove(SCRATCH_CACHE_PATH);
    (void)loggerShutdown();
    (void)mmShutdown();
    return 0;
}

/* ── Test cases ──────────────────────────────────────────────────────── */

static void test_save_main_db_to_path(void)
{
    MainCache mc;
    Stock st;
    status_t r;

    (void)mainCacheInit(&mc);
    memset(&st, 0, sizeof(st));
    (void)safe_strcpy(st.symbol, sizeof(st.symbol), "PSTX");
    (void)safe_strcpy(st.name,   sizeof(st.name),   "Persist Test Co");
    st.price = 33.5;
    (void)mainCacheAdd(&mc, &st);

    r = saveMainDbToPath(&mc, SCRATCH_STOCK_PATH);
    CU_ASSERT_EQUAL(r, STATUS_OK);

    (void)mainCacheDestroy(&mc);
}

static void test_load_main_db_roundtrips_price(void)
{
    MainCache mc2;
    Stock found;
    status_t r;

    /* The scratch file was written by the previous test */
    (void)mainCacheInit(&mc2);
    r = loadMainDbFromPath(&mc2, SCRATCH_STOCK_PATH);
    CU_ASSERT_EQUAL(r, STATUS_OK);

    r = mainCacheSearch(&mc2, "PSTX", &found);
    CU_ASSERT_EQUAL(r, STATUS_OK);
    CU_ASSERT_DOUBLE_EQUAL(found.price, 33.5, 0.001);

    (void)mainCacheDestroy(&mc2);
}

static void test_load_main_db_roundtrips_symbol_name(void)
{
    MainCache mc3;
    Stock found;

    (void)mainCacheInit(&mc3);
    (void)loadMainDbFromPath(&mc3, SCRATCH_STOCK_PATH);
    (void)mainCacheSearch(&mc3, "PSTX", &found);

    CU_ASSERT_STRING_EQUAL(found.symbol, "PSTX");
    CU_ASSERT_STRING_EQUAL(found.name,   "Persist Test Co");

    (void)mainCacheDestroy(&mc3);
}

static void test_save_search_cache_to_path(void)
{
    SearchCache sc;
    Stock st;
    status_t r;

    (void)searchCacheInit(&sc);
    memset(&st, 0, sizeof(st));
    (void)safe_strcpy(st.symbol, sizeof(st.symbol), "PSTX");
    (void)safe_strcpy(st.name,   sizeof(st.name),   "Persist Test Co");
    st.price = 33.5;
    (void)searchCacheTouch(&sc, &st);

    r = saveCacheToPath(&sc, SCRATCH_CACHE_PATH);
    CU_ASSERT_EQUAL(r, STATUS_OK);

    (void)searchCacheDestroy(&sc);
}

static void test_load_search_cache_roundtrips_entry(void)
{
    SearchCache scLoaded;
    status_t r;

    (void)searchCacheInit(&scLoaded);
    r = loadCacheFromPath(&scLoaded, SCRATCH_CACHE_PATH);
    CU_ASSERT_EQUAL(r, STATUS_OK);
    CU_ASSERT_TRUE(searchCacheContains(&scLoaded, "PSTX"));

    (void)searchCacheDestroy(&scLoaded);
}

static void test_load_nonexistent_file_returns_io_error(void)
{
    MainCache mc;
    (void)mainCacheInit(&mc);
    status_t r = loadMainDbFromPath(&mc, "/tmp/this_file_does_not_exist_cunit.db");
    CU_ASSERT_EQUAL(r, STATUS_ERR_IO);
    (void)mainCacheDestroy(&mc);
}

static void test_save_null_cache_returns_invalid_arg(void)
{
    status_t r = saveMainDbToPath(NULL, SCRATCH_STOCK_PATH);
    CU_ASSERT_EQUAL(r, STATUS_ERR_INVALID_ARG);
}

static void test_save_null_path_returns_invalid_arg(void)
{
    MainCache mc;
    (void)mainCacheInit(&mc);
    status_t r = saveMainDbToPath(&mc, NULL);
    CU_ASSERT_EQUAL(r, STATUS_ERR_INVALID_ARG);
    (void)mainCacheDestroy(&mc);
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

    suite = CU_add_suite("Persistence", suite_setup, suite_teardown);
    if (suite == NULL)
    {
        CU_cleanup_registry();
        return (int)CU_get_error();
    }

    /* Tests run in order: save first, then load */
    if ((CU_add_test(suite, "Save main DB to scratch path succeeds",          test_save_main_db_to_path)              == NULL) ||
        (CU_add_test(suite, "Reload main DB round-trips price correctly",     test_load_main_db_roundtrips_price)     == NULL) ||
        (CU_add_test(suite, "Reload main DB round-trips symbol and name",     test_load_main_db_roundtrips_symbol_name) == NULL) ||
        (CU_add_test(suite, "Save search cache to scratch path succeeds",     test_save_search_cache_to_path)         == NULL) ||
        (CU_add_test(suite, "Reload search cache round-trips entry (PSTX)",  test_load_search_cache_roundtrips_entry)== NULL) ||
        (CU_add_test(suite, "Load nonexistent file returns STATUS_ERR_IO",   test_load_nonexistent_file_returns_io_error) == NULL) ||
        (CU_add_test(suite, "Save NULL cache returns STATUS_ERR_INVALID_ARG",test_save_null_cache_returns_invalid_arg) == NULL) ||
        (CU_add_test(suite, "Save NULL path returns STATUS_ERR_INVALID_ARG", test_save_null_path_returns_invalid_arg) == NULL))
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
