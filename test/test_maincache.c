/*
 * test_maincache.c — CUnit tests for MainCache CRUD operations
 * Mirrors: unitTest2_MainCache() in src/testing.c
 *
 * ── Compile & run (from the test/ directory) ──────────────────────────
 *   gcc -std=c11 -Wall -Wextra -I../include -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
 *       test_maincache.c \
 *       ../src/cache.c ../src/common.c ../src/memory.c \
 *       -lcunit -lpthread -o test_maincache
 *   ./test_maincache
 *
 * ── Headers used ──────────────────────────────────────────────────────
 *   cache.h   — MainCache, mainCacheInit/Add/Search/Update/Delete/…
 *   common.h  — status_t, Stock, safe_strcpy(), …
 *   memory.h  — mmInit(), mmAlloc(), mmFree(), mmShutdown()
 *
 * ── Sources compiled ──────────────────────────────────────────────────
 *   ../src/cache.c   — MainCache implementation
 *   ../src/common.c  — safe_strcpy(), status_to_string(), …
 *   ../src/memory.c  — mmAlloc(), mmFree() (used internally by cache)
 */

#include "common.h"
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <string.h>
#include "cache.h"
#include "memory.h"

/* Module-level cache: initialised in suite_setup, destroyed in suite_teardown */
static MainCache g_mc;

static int suite_setup(void)
{
    (void)mmInit();
    return (mainCacheInit(&g_mc) == STATUS_OK) ? 0 : 1;
}

static int suite_teardown(void)
{
    (void)mainCacheDestroy(&g_mc);
    (void)mmShutdown();
    return 0;
}

/* Helper: build a Stock with a given symbol and price */
static Stock make_stock(const char *symbol, const char *name, double price)
{
    Stock st;
    memset(&st, 0, sizeof(st));
    (void)safe_strcpy(st.symbol, sizeof(st.symbol), symbol);
    (void)safe_strcpy(st.name,   sizeof(st.name),   name);
    st.price = price;
    return st;
}

/* ── Test cases ──────────────────────────────────────────────────────── */

static void test_add_new_stock(void)
{
    Stock st = make_stock("ZZZT", "Zzz Test Corp", 50.0);
    status_t r = mainCacheAdd(&g_mc, &st);
    CU_ASSERT_EQUAL(r, STATUS_OK);
}

static void test_search_finds_added_stock(void)
{
    Stock found;
    status_t r = mainCacheSearch(&g_mc, "ZZZT", &found);
    CU_ASSERT_EQUAL(r, STATUS_OK);
    CU_ASSERT_DOUBLE_EQUAL(found.price, 50.0, 0.001);
}

static void test_add_duplicate_rejected(void)
{
    Stock st = make_stock("ZZZT", "Zzz Test Corp", 50.0);
    status_t r = mainCacheAdd(&g_mc, &st);
    CU_ASSERT_EQUAL(r, STATUS_ERR_DUPLICATE);
}

static void test_update_price_applies(void)
{
    Stock found;
    status_t r = mainCacheUpdatePrice(&g_mc, "ZZZT", 75.0);
    CU_ASSERT_EQUAL(r, STATUS_OK);
    (void)mainCacheSearch(&g_mc, "ZZZT", &found);
    CU_ASSERT_DOUBLE_EQUAL(found.price, 75.0, 0.001);
}

static void test_update_price_unknown_symbol(void)
{
    status_t r = mainCacheUpdatePrice(&g_mc, "XXXX", 100.0);
    CU_ASSERT_EQUAL(r, STATUS_ERR_NOT_FOUND);
}

static void test_contains_returns_true(void)
{
    CU_ASSERT_TRUE(mainCacheContains(&g_mc, "ZZZT"));
}

static void test_contains_returns_false_for_unknown(void)
{
    CU_ASSERT_FALSE(mainCacheContains(&g_mc, "NONE"));
}

static void test_delete_removes_stock(void)
{
    status_t r = mainCacheDelete(&g_mc, "ZZZT");
    CU_ASSERT_EQUAL(r, STATUS_OK);
    CU_ASSERT_FALSE(mainCacheContains(&g_mc, "ZZZT"));
}

static void test_delete_unknown_symbol(void)
{
    status_t r = mainCacheDelete(&g_mc, "XXXX");
    CU_ASSERT_EQUAL(r, STATUS_ERR_NOT_FOUND);
}

static void test_search_after_delete_not_found(void)
{
    Stock found;
    status_t r = mainCacheSearch(&g_mc, "ZZZT", &found);
    CU_ASSERT_EQUAL(r, STATUS_ERR_NOT_FOUND);
}

static void test_count_increments_on_add(void)
{
    size_t before = mainCacheCount(&g_mc);
    Stock st = make_stock("CNTT", "Count Test Co", 10.0);
    (void)mainCacheAdd(&g_mc, &st);
    CU_ASSERT_EQUAL(mainCacheCount(&g_mc), before + 1U);
    (void)mainCacheDelete(&g_mc, "CNTT");
}

static void test_snapshot_contains_added_stocks(void)
{
    Stock st = make_stock("SNAP", "Snapshot Co", 99.0);
    (void)mainCacheAdd(&g_mc, &st);

    Stock *arr = NULL;
    size_t count = 0U;
    status_t r = mainCacheSnapshot(&g_mc, &arr, &count);
    CU_ASSERT_EQUAL(r, STATUS_OK);
    CU_ASSERT(count >= 1U);
    CU_ASSERT_PTR_NOT_NULL(arr);
    if (arr != NULL) { mmFree(arr); }

    (void)mainCacheDelete(&g_mc, "SNAP");
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

    suite = CU_add_suite("Main_Cache", suite_setup, suite_teardown);
    if (suite == NULL)
    {
        CU_cleanup_registry();
        return (int)CU_get_error();
    }

    /* Tests run in declaration order — add before search/update/delete */
    if ((CU_add_test(suite, "Add new stock returns STATUS_OK",           test_add_new_stock)              == NULL) ||
        (CU_add_test(suite, "Search finds the added stock",              test_search_finds_added_stock)   == NULL) ||
        (CU_add_test(suite, "Add duplicate returns STATUS_ERR_DUPLICATE",test_add_duplicate_rejected)     == NULL) ||
        (CU_add_test(suite, "Update price reflects in search",           test_update_price_applies)       == NULL) ||
        (CU_add_test(suite, "Update unknown symbol returns NOT_FOUND",   test_update_price_unknown_symbol)== NULL) ||
        (CU_add_test(suite, "Contains returns true for present stock",   test_contains_returns_true)      == NULL) ||
        (CU_add_test(suite, "Contains returns false for absent stock",   test_contains_returns_false_for_unknown) == NULL) ||
        (CU_add_test(suite, "Delete removes stock from cache",           test_delete_removes_stock)       == NULL) ||
        (CU_add_test(suite, "Delete unknown symbol returns NOT_FOUND",   test_delete_unknown_symbol)      == NULL) ||
        (CU_add_test(suite, "Search after delete returns NOT_FOUND",     test_search_after_delete_not_found) == NULL) ||
        (CU_add_test(suite, "Count increments on add",                   test_count_increments_on_add)    == NULL) ||
        (CU_add_test(suite, "Snapshot includes all added stocks",        test_snapshot_contains_added_stocks) == NULL))
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
