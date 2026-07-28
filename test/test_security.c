/*
 * test_security.c — CUnit tests for all input-validation functions
 * Mirrors: testerRunSecurityValidationTests() in src/testing.c
 *
 * ── Compile & run (from the test/ directory) ──────────────────────────
 *   gcc -std=c11 -Wall -Wextra -I../include -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
 *       test_security.c \
 *       ../src/security.c ../src/common.c \
 *       -lcunit -lpthread -o test_security
 *   ./test_security
 *
 * ── Headers used ──────────────────────────────────────────────────────
 *   security.h — secValidateSymbol/Price/Username/Password/MenuChoice/
 *                secValidateCompanyName
 *   common.h   — portable_strnlen(), SYMBOL_MAX_LEN, …
 *
 * ── Sources compiled ──────────────────────────────────────────────────
 *   ../src/security.c — all secValidate* implementations
 *   ../src/common.c   — portable_strnlen(), safe_strcpy(), …
 */

#include "common.h"
#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>
#include "security.h"

/* ── secValidateSymbol ───────────────────────────────────────────────── */

static void test_symbol_valid(void)       { CU_ASSERT_TRUE(secValidateSymbol("AAPL")); }
static void test_symbol_single_char(void) { CU_ASSERT_TRUE(secValidateSymbol("V")); }
static void test_symbol_max_length(void)  { CU_ASSERT_TRUE(secValidateSymbol("ABCDEFG")); /* 7 chars */ }
static void test_symbol_empty(void)       { CU_ASSERT_FALSE(secValidateSymbol("")); }
static void test_symbol_null(void)        { CU_ASSERT_FALSE(secValidateSymbol(NULL)); }
static void test_symbol_invalid_at(void)  { CU_ASSERT_FALSE(secValidateSymbol("AB@L")); }
static void test_symbol_invalid_space(void){ CU_ASSERT_FALSE(secValidateSymbol("AB L")); }
static void test_symbol_too_long(void)    { CU_ASSERT_FALSE(secValidateSymbol("TOOLONGSYMBOL")); }

/* ── secValidatePrice ────────────────────────────────────────────────── */

static void test_price_valid_100(void)      { CU_ASSERT_TRUE(secValidatePrice(100.0)); }
static void test_price_valid_min(void)      { CU_ASSERT_TRUE(secValidatePrice(MIN_PRICE)); }
static void test_price_valid_max(void)      { CU_ASSERT_TRUE(secValidatePrice(MAX_PRICE)); }
static void test_price_negative(void)       { CU_ASSERT_FALSE(secValidatePrice(-5.0)); }
static void test_price_zero(void)           { CU_ASSERT_FALSE(secValidatePrice(0.0)); }
static void test_price_too_large(void)      { CU_ASSERT_FALSE(secValidatePrice(1.0e12)); }

/* ── secValidateUsername ─────────────────────────────────────────────── */

static void test_username_valid(void)          { CU_ASSERT_TRUE(secValidateUsername("admin_1")); }
static void test_username_with_dot(void)       { CU_ASSERT_TRUE(secValidateUsername("user.name")); }
static void test_username_empty(void)          { CU_ASSERT_FALSE(secValidateUsername("")); }
static void test_username_null(void)           { CU_ASSERT_FALSE(secValidateUsername(NULL)); }
static void test_username_with_space(void)     { CU_ASSERT_FALSE(secValidateUsername("bad user")); }
static void test_username_with_at(void)        { CU_ASSERT_FALSE(secValidateUsername("bad@user")); }

/* ── secValidatePassword ─────────────────────────────────────────────── */

static void test_password_valid(void)          { CU_ASSERT_TRUE(secValidatePassword("passw0rd")); }
static void test_password_exactly_min(void)    { CU_ASSERT_TRUE(secValidatePassword("abcd")); /* 4 chars */ }
static void test_password_too_short(void)      { CU_ASSERT_FALSE(secValidatePassword("ab")); }
static void test_password_null(void)           { CU_ASSERT_FALSE(secValidatePassword(NULL)); }
static void test_password_with_space(void)     { CU_ASSERT_FALSE(secValidatePassword("bad pass")); }
static void test_password_with_tab(void)       { CU_ASSERT_FALSE(secValidatePassword("bad\tpass")); }

/* ── secValidateMenuChoice ───────────────────────────────────────────── */

static void test_menu_choice_in_bounds(void)   { CU_ASSERT_TRUE(secValidateMenuChoice(3, 0, 10)); }
static void test_menu_choice_at_min(void)      { CU_ASSERT_TRUE(secValidateMenuChoice(0, 0, 10)); }
static void test_menu_choice_at_max(void)      { CU_ASSERT_TRUE(secValidateMenuChoice(10, 0, 10)); }
static void test_menu_choice_below_min(void)   { CU_ASSERT_FALSE(secValidateMenuChoice(-1, 0, 10)); }
static void test_menu_choice_above_max(void)   { CU_ASSERT_FALSE(secValidateMenuChoice(11, 0, 10)); }

/* ── secValidateCompanyName ──────────────────────────────────────────── */

static void test_company_name_valid(void)      { CU_ASSERT_TRUE(secValidateCompanyName("Apple Inc.")); }
static void test_company_name_empty(void)      { CU_ASSERT_FALSE(secValidateCompanyName("")); }
static void test_company_name_null(void)       { CU_ASSERT_FALSE(secValidateCompanyName(NULL)); }

/* ── main ────────────────────────────────────────────────────────────── */

int main(void)
{
    CU_pSuite sSymbol = NULL, sPrice = NULL, sUser = NULL;
    CU_pSuite sPass   = NULL, sMenu  = NULL, sName = NULL;
    unsigned int failures;

    if (CU_initialize_registry() != CUE_SUCCESS)
    {
        return (int)CU_get_error();
    }

    /* One suite per validator group */
    sSymbol = CU_add_suite("secValidateSymbol",      NULL, NULL);
    sPrice  = CU_add_suite("secValidatePrice",       NULL, NULL);
    sUser   = CU_add_suite("secValidateUsername",    NULL, NULL);
    sPass   = CU_add_suite("secValidatePassword",    NULL, NULL);
    sMenu   = CU_add_suite("secValidateMenuChoice",  NULL, NULL);
    sName   = CU_add_suite("secValidateCompanyName", NULL, NULL);

    if ((sSymbol == NULL) || (sPrice == NULL) || (sUser == NULL) ||
        (sPass   == NULL) || (sMenu  == NULL) || (sName == NULL))
    {
        CU_cleanup_registry();
        return (int)CU_get_error();
    }

    /* Symbol suite */
    CU_add_test(sSymbol, "Valid symbol 'AAPL' accepted",          test_symbol_valid);
    CU_add_test(sSymbol, "Single-char symbol 'V' accepted",       test_symbol_single_char);
    CU_add_test(sSymbol, "7-char symbol accepted (max length)",   test_symbol_max_length);
    CU_add_test(sSymbol, "Empty symbol rejected",                  test_symbol_empty);
    CU_add_test(sSymbol, "NULL symbol rejected",                   test_symbol_null);
    CU_add_test(sSymbol, "Symbol with '@' rejected",               test_symbol_invalid_at);
    CU_add_test(sSymbol, "Symbol with space rejected",             test_symbol_invalid_space);
    CU_add_test(sSymbol, "Overlong symbol rejected",               test_symbol_too_long);

    /* Price suite */
    CU_add_test(sPrice, "Valid price 100.0 accepted",              test_price_valid_100);
    CU_add_test(sPrice, "MIN_PRICE boundary accepted",             test_price_valid_min);
    CU_add_test(sPrice, "MAX_PRICE boundary accepted",             test_price_valid_max);
    CU_add_test(sPrice, "Negative price rejected",                 test_price_negative);
    CU_add_test(sPrice, "Zero price rejected",                     test_price_zero);
    CU_add_test(sPrice, "Absurdly large price rejected",           test_price_too_large);

    /* Username suite */
    CU_add_test(sUser, "Valid username 'admin_1' accepted",        test_username_valid);
    CU_add_test(sUser, "Username with dot accepted",               test_username_with_dot);
    CU_add_test(sUser, "Empty username rejected",                  test_username_empty);
    CU_add_test(sUser, "NULL username rejected",                   test_username_null);
    CU_add_test(sUser, "Username with space rejected",             test_username_with_space);
    CU_add_test(sUser, "Username with '@' rejected",               test_username_with_at);

    /* Password suite */
    CU_add_test(sPass, "Valid password 'passw0rd' accepted",       test_password_valid);
    CU_add_test(sPass, "Exactly-minimum-length password accepted", test_password_exactly_min);
    CU_add_test(sPass, "Too-short password rejected",              test_password_too_short);
    CU_add_test(sPass, "NULL password rejected",                   test_password_null);
    CU_add_test(sPass, "Password with space rejected",             test_password_with_space);
    CU_add_test(sPass, "Password with tab rejected",               test_password_with_tab);

    /* Menu choice suite */
    CU_add_test(sMenu, "Choice within bounds accepted",            test_menu_choice_in_bounds);
    CU_add_test(sMenu, "Choice at minimum boundary accepted",      test_menu_choice_at_min);
    CU_add_test(sMenu, "Choice at maximum boundary accepted",      test_menu_choice_at_max);
    CU_add_test(sMenu, "Choice below minimum rejected",            test_menu_choice_below_min);
    CU_add_test(sMenu, "Choice above maximum rejected",            test_menu_choice_above_max);

    /* Company name suite */
    CU_add_test(sName, "Valid company name accepted",              test_company_name_valid);
    CU_add_test(sName, "Empty company name rejected",              test_company_name_empty);
    CU_add_test(sName, "NULL company name rejected",               test_company_name_null);

    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();

    failures = CU_get_number_of_failures();
    CU_cleanup_registry();
    return (int)failures;
}
