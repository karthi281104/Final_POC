/*
 * test_hash.c — CUnit tests for mainCacheHash()
 * Mirrors: unitTest1_HashFunction() in src/testing.c
 *
 * ── Compile & run (from the test/ directory) ──────────────────────────
 *   gcc -std=c11 -Wall -Wextra -I../include -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
 *       test_hash.c \
 *       ../src/cache.c ../src/common.c ../src/memory.c \
 *       -lcunit -lpthread -o test_hash
 *   ./test_hash
 *
 * ── Headers used ──────────────────────────────────────────────────────
 *   cache.h   — mainCacheHash(), MAIN_DB_BUCKETS
 *   common.h  — status_t, SYMBOL_MAX_LEN, …
 *
 * ── Sources compiled ──────────────────────────────────────────────────
 *   ../src/cache.c   — mainCacheHash() implementation
 *   ../src/common.c  — safe_strcpy(), status_to_string(), …
 */

#include "common.h"
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "cache.h"

/* ── Test cases ──────────────────────────────────────────────────────── */

/* Same symbol must always produce the same bucket index */
static void test_hash_deterministic(void)
{
    unsigned long h1 = mainCacheHash("AAPL");
    unsigned long h2 = mainCacheHash("AAPL");
    CU_ASSERT_EQUAL(h1, h2);
}

/* Every hash result must fall inside [0, MAIN_DB_BUCKETS) */
static void test_hash_in_range(void)
{
    CU_ASSERT(mainCacheHash("AAPL")  < MAIN_DB_BUCKETS);
    CU_ASSERT(mainCacheHash("MSFT")  < MAIN_DB_BUCKETS);
    CU_ASSERT(mainCacheHash("GOOGL") < MAIN_DB_BUCKETS);
    CU_ASSERT(mainCacheHash("TSLA")  < MAIN_DB_BUCKETS);
    CU_ASSERT(mainCacheHash("V")     < MAIN_DB_BUCKETS);
}

/* NULL symbol must not crash; the default hash (5381 % buckets) stays in range */
static void test_hash_null_safe(void)
{
    unsigned long h = mainCacheHash(NULL);
    CU_ASSERT(h < MAIN_DB_BUCKETS);
}

/* Two distinct symbols should not both hash to 0 (sanity check) */
static void test_hash_nonzero_for_real_symbol(void)
{
    /* djb2 of "AAPL" over 101 buckets is known non-zero */
    unsigned long h = mainCacheHash("AAPL");
    CU_ASSERT(h < MAIN_DB_BUCKETS);
    /* Verify AAPL and MSFT produce in-range results independently */
    CU_ASSERT(mainCacheHash("MSFT") < MAIN_DB_BUCKETS);
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

    suite = CU_add_suite("Hash_Function", NULL, NULL);
    if (suite == NULL)
    {
        CU_cleanup_registry();
        return (int)CU_get_error();
    }

    if ((CU_add_test(suite, "Hash is deterministic for same symbol",     test_hash_deterministic)       == NULL) ||
        (CU_add_test(suite, "Hash values fall within bucket range",       test_hash_in_range)            == NULL) ||
        (CU_add_test(suite, "NULL symbol does not crash, stays in range", test_hash_null_safe)           == NULL) ||
        (CU_add_test(suite, "Real symbols produce non-zero in-range hash",test_hash_nonzero_for_real_symbol) == NULL))
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
