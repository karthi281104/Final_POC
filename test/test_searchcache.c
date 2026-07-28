/*
 * test_searchcache.c — CUnit tests for SearchCache LRU eviction behaviour
 * Mirrors: unitTest3_SearchCacheLRU() in src/testing.c
 *
 * ── Compile & run (from the test/ directory) ──────────────────────────
 *   gcc -std=c11 -Wall -Wextra -I../include -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
 *       test_searchcache.c \
 *       ../src/searchcache.c ../src/common.c ../src/memory.c \
 *       -lcunit -lpthread -o test_searchcache
 *   ./test_searchcache
 *
 * ── Headers used ──────────────────────────────────────────────────────
 *   searchcache.h — SearchCache, searchCacheInit/Touch/Search/Destroy/…
 *   common.h      — status_t, Stock, safe_strcpy(), SEARCH_CACHE_CAPACITY
 *   memory.h      — mmInit(), mmAlloc(), mmFree(), mmShutdown()
 *
 * ── Sources compiled ──────────────────────────────────────────────────
 *   ../src/searchcache.c — LRU doubly-linked list implementation
 *   ../src/common.c      — safe_strcpy(), safe_strcasecmp(), …
 *   ../src/memory.c      — mmAlloc(), mmFree() (used by searchcache)
 */

#include "common.h"
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include <string.h>
#include <stdio.h>
#include "searchcache.h"
#include "memory.h"

static SearchCache g_sc;

static int suite_setup(void)
{
    (void)mmInit();
    return (searchCacheInit(&g_sc) == STATUS_OK) ? 0 : 1;
}

static int suite_teardown(void)
{
    (void)searchCacheDestroy(&g_sc);
    (void)mmShutdown();
    return 0;
}

static Stock make_stock(const char *symbol, double price)
{
    Stock st;
    memset(&st, 0, sizeof(st));
    (void)safe_strcpy(st.symbol, sizeof(st.symbol), symbol);
    (void)safe_strcpy(st.name,   sizeof(st.name),   "LRU Test");
    st.price = price;
    return st;
}

/* Fill cache with CAPACITY + 2 unique symbols: S000, S001, … S011 */
static void fill_past_capacity(void)
{
    size_t i;
    for (i = 0U; i < (SEARCH_CACHE_CAPACITY + 2U); i++)
    {
        char sym[SYMBOL_MAX_LEN];
        Stock st;
        (void)snprintf(sym, sizeof(sym), "S%03u", (unsigned)i);
        st = make_stock(sym, 1.0);
        (void)searchCacheTouch(&g_sc, &st);
    }
}

/* ── Test cases ──────────────────────────────────────────────────────── */

static void test_cache_caps_at_capacity(void)
{
    fill_past_capacity();
    CU_ASSERT_EQUAL(searchCacheCount(&g_sc), SEARCH_CACHE_CAPACITY);
}

static void test_oldest_entry_evicted(void)
{
    /* S000 was the first ever insert; must have been evicted */
    Stock found;
    status_t r = searchCacheSearch(&g_sc, "S000", &found);
    CU_ASSERT_NOT_EQUAL(r, STATUS_OK);
}

static void test_most_recent_entry_present(void)
{
    /* S011 is the most recent insert; must still be present */
    Stock found;
    status_t r = searchCacheSearch(&g_sc, "S011", &found);
    CU_ASSERT_EQUAL(r, STATUS_OK);
}

static void test_move_to_front_protects_from_eviction(void)
{
    /* Re-touch S002 to promote it to most-recently-used */
    Stock st = make_stock("S002", 2.0);
    (void)searchCacheTouch(&g_sc, &st);

    /* Insert one brand-new symbol to force one eviction */
    Stock snew = make_stock("SNEW", 9.0);
    (void)searchCacheTouch(&g_sc, &snew);

    /* S002 was just promoted so it must survive the eviction */
    Stock found;
    status_t r = searchCacheSearch(&g_sc, "S002", &found);
    CU_ASSERT_EQUAL(r, STATUS_OK);
}

static void test_count_stays_at_capacity_after_overflow(void)
{
    CU_ASSERT_EQUAL(searchCacheCount(&g_sc), SEARCH_CACHE_CAPACITY);
}

static void test_update_price_in_cache(void)
{
    /* SNEW was added in the move-to-front test; update its price */
    status_t r = searchCacheUpdatePrice(&g_sc, "SNEW", 99.0);
    CU_ASSERT_EQUAL(r, STATUS_OK);

    Stock found;
    (void)searchCacheSearch(&g_sc, "SNEW", &found);
    CU_ASSERT_DOUBLE_EQUAL(found.price, 99.0, 0.001);
}

static void test_update_price_unknown_symbol(void)
{
    status_t r = searchCacheUpdatePrice(&g_sc, "NONE", 1.0);
    CU_ASSERT_EQUAL(r, STATUS_ERR_NOT_FOUND);
}

static void test_contains_wrapper(void)
{
    CU_ASSERT_TRUE(searchCacheContains(&g_sc, "SNEW"));
    CU_ASSERT_FALSE(searchCacheContains(&g_sc, "S000")); /* evicted */
}

static void test_snapshot_returns_all_entries(void)
{
    Stock *arr = NULL;
    size_t count = 0U;
    status_t r = searchCacheSnapshot(&g_sc, &arr, &count);
    CU_ASSERT_EQUAL(r, STATUS_OK);
    CU_ASSERT_EQUAL(count, SEARCH_CACHE_CAPACITY);
    CU_ASSERT_PTR_NOT_NULL(arr);
    if (arr != NULL) { mmFree(arr); }
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

    suite = CU_add_suite("SearchCache_LRU", suite_setup, suite_teardown);
    if (suite == NULL)
    {
        CU_cleanup_registry();
        return (int)CU_get_error();
    }

    if ((CU_add_test(suite, "Cache caps at SEARCH_CACHE_CAPACITY after overflow",  test_cache_caps_at_capacity)          == NULL) ||
        (CU_add_test(suite, "Oldest entry was evicted (S000)",                     test_oldest_entry_evicted)            == NULL) ||
        (CU_add_test(suite, "Most recent entry still present (S011)",              test_most_recent_entry_present)       == NULL) ||
        (CU_add_test(suite, "Move-to-front protects re-touched entry (S002)",      test_move_to_front_protects_from_eviction) == NULL) ||
        (CU_add_test(suite, "Count stays at capacity after further inserts",       test_count_stays_at_capacity_after_overflow) == NULL) ||
        (CU_add_test(suite, "Update price of cached symbol succeeds",              test_update_price_in_cache)           == NULL) ||
        (CU_add_test(suite, "Update price of absent symbol returns NOT_FOUND",     test_update_price_unknown_symbol)     == NULL) ||
        (CU_add_test(suite, "Contains wrapper returns correct true/false",         test_contains_wrapper)                == NULL) ||
        (CU_add_test(suite, "Snapshot returns all CAPACITY entries",               test_snapshot_returns_all_entries)    == NULL))
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
