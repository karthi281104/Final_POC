#include "common.h"
#include "memory.h"
#include "logger.h"
#include "security.h"
#include "cache.h"
#include "searchcache.h"
#include "persistence.h"
#include "stats.h"
#include "alerts.h"
#include "auth.h"
#include "query.h"
#include "feed.h"
#include "thread_manager.h"
#include "testing.h"
#include "diagnostics.h"
#include "optimizer.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>

#if defined(_WIN32) || defined(_WIN64)
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#include <sys/types.h>
#define MKDIR(path) mkdir((path), 0755)
#endif

/* ===================== Application context =============================
 * Bundles every store together so we can pass one pointer around
 * instead of five, and so save/shutdown code has everything at hand. */
typedef struct {
    MainCache mainDb;
    SearchCache searchDb;
    AlertStore alerts;
    Stats stats;
    UserStore users;
    ThreadManager tm;
    User currentUser;
    bool hasLoggedInUser; /* becomes true after the first successful login */
} AppContext;

static void ensureDirectories(void)
{
    (void)MKDIR("data");
    (void)MKDIR("logs");
}

static bool fileExists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0);
}

static void pauseForUser(void)
{
    (void)printf("\nPress ENTER to continue...");
    (void)fflush(stdout);
    {
        int c;
        while (((c = getchar()) != '\n') && (c != EOF))
        {
            /* discard */
        }
    }
}

static int readIntChoice(const char *prompt)
{
    char line[LINE_MAX_LEN];
    int value = -1;

    (void)printf("%s", prompt);
    (void)fflush(stdout);

    if (fgets(line, (int)sizeof(line), stdin) != NULL)
    {
        value = atoi(line);
    }
    return value;
}

static void readLine(const char *prompt, char *buf, size_t bufSize)
{
    (void)printf("%s", prompt);
    (void)fflush(stdout);
    if (fgets(buf, (int)bufSize, stdin) != NULL)
    {
        size_t len = strlen(buf);
        while ((len > 0U) && ((buf[len - 1U] == '\n') || (buf[len - 1U] == '\r')))
        {
            buf[len - 1U] = '\0';
            len--;
        }
    }
    else
    {
        buf[0] = '\0';
    }
}

static double readDouble(const char *prompt)
{
    char line[LINE_MAX_LEN];
    double value = 0.0;

    (void)printf("%s", prompt);
    (void)fflush(stdout);
    if (fgets(line, (int)sizeof(line), stdin) != NULL)
    {
        value = atof(line);
    }
    return value;
}

/* ===================== Per-user cache/alerts paths =======================
 * Each logged-in user gets their OWN search cache and OWN alert store,
 * saved as data/cache_<username>.db and data/alerts_<username>.db, so
 * an admin's recently-searched list and alerts never mix with a
 * regular user's (or another user's). The shared main database
 * (data/stock.db) and users.txt remain global, since every account
 * looks at the same authoritative stock data. */
static void buildUserScopedPath(const char *prefix, const char *username,
                                 char *buf, size_t bufSize)
{
    (void)snprintf(buf, bufSize, "data/%s_%s.db", prefix, username);
}

/* Loads (or starts fresh for) the current user's own cache and alerts.
 * Must only be called while the background threads are NOT running,
 * since it destroys and re-initializes the shared SearchCache/AlertStore
 * structures (including their internal locks) that those threads read
 * and write concurrently. */
static void loadUserSession(AppContext *ctx)
{
    char cachePath[PATH_MAX_LEN];
    char alertsPath[PATH_MAX_LEN];

    (void)searchCacheDestroy(&ctx->searchDb);
    (void)searchCacheInit(&ctx->searchDb);
    (void)alertsDestroy(&ctx->alerts);
    (void)alertsInit(&ctx->alerts);

    buildUserScopedPath("cache", ctx->currentUser.username, cachePath, sizeof(cachePath));
    buildUserScopedPath("alerts", ctx->currentUser.username, alertsPath, sizeof(alertsPath));

    if (fileExists(cachePath))
    {
        (void)loadCacheFromPath(&ctx->searchDb, cachePath);
    }
    if (fileExists(alertsPath))
    {
        (void)alertsLoadFromPath(&ctx->alerts, alertsPath);
    }

    (void)loggerLog(LOG_AUDIT, "Loaded per-user cache (%zu entries) and alerts (%zu) for %s",
                     searchCacheCount(&ctx->searchDb), alertsCount(&ctx->alerts),
                     ctx->currentUser.username);
}

/* Saves the current user's own cache and alerts back to their
 * user-scoped files. Safe to call while background threads are
 * running: the underlying save functions lock the same mutexes the
 * threads use, they just don't destroy/recreate the structures. */
static void saveUserSession(AppContext *ctx)
{
    char cachePath[PATH_MAX_LEN];
    char alertsPath[PATH_MAX_LEN];

    buildUserScopedPath("cache", ctx->currentUser.username, cachePath, sizeof(cachePath));
    buildUserScopedPath("alerts", ctx->currentUser.username, alertsPath, sizeof(alertsPath));

    (void)saveCacheToPath(&ctx->searchDb, cachePath);
    (void)alertsSaveToPath(&ctx->alerts, alertsPath);

    (void)loggerLog(LOG_AUDIT, "Saved per-user cache and alerts for %s", ctx->currentUser.username);
}

/* ===================== Bootstrap / shutdown ============================= */

static status_t bootstrap(AppContext *ctx)
{
    status_t result = STATUS_OK;

    ensureDirectories();
    (void)mmInit();
    (void)loggerInit();

    (void)mainCacheInit(&ctx->mainDb);
    (void)searchCacheInit(&ctx->searchDb);
    (void)alertsInit(&ctx->alerts);
    (void)statsInit(&ctx->stats);
    (void)authInit(&ctx->users);
    ctx->hasLoggedInUser = false;

    if (fileExists(DEFAULT_STOCK_DB_PATH))
    {
        (void)loadMainDb(&ctx->mainDb);
        (void)loggerLog(LOG_AUDIT, "Loaded main DB from %s (%zu stocks)",
                         DEFAULT_STOCK_DB_PATH, mainCacheCount(&ctx->mainDb));
    }
    else
    {
        (void)persistenceSeedMainDb(&ctx->mainDb);
        (void)saveMainDb(&ctx->mainDb);
        (void)loggerLog(LOG_AUDIT, "First run: seeded and saved main DB with %zu stocks",
                         mainCacheCount(&ctx->mainDb));
    }

    /* Cache and alerts are per-user and are loaded only after a
     * successful login (see loadUserSession), not here. */

    (void)authLoadUsers(&ctx->users);

    (void)tmInit(&ctx->tm, &ctx->mainDb, &ctx->searchDb, &ctx->alerts, &ctx->stats);
    (void)tmStartAll(&ctx->tm);

    (void)srand((unsigned int)time(NULL));

    return result;
}

static void persistAll(AppContext *ctx)
{
    (void)saveMainDb(&ctx->mainDb);
    if (ctx->hasLoggedInUser)
    {
        saveUserSession(ctx);
    }
    (void)loggerLog(LOG_AUDIT, "All databases saved to disk");
}

static void appShutdown(AppContext *ctx)
{
    (void)tmStopAll(&ctx->tm);
    persistAll(ctx);

    (void)tmDestroy(&ctx->tm);
    (void)mainCacheDestroy(&ctx->mainDb);
    (void)searchCacheDestroy(&ctx->searchDb);
    (void)alertsDestroy(&ctx->alerts);
    (void)statsDestroy(&ctx->stats);

    (void)loggerShutdown();
    (void)mmShutdown();
}

/* ===================== Shared feature actions ============================ */

static void actionSearchStock(AppContext *ctx)
{
    char symbol[SYMBOL_MAX_LEN];
    Stock found;
    location_status_t loc;
    status_t r;

    readLine("Enter symbol to search: ", symbol, sizeof(symbol));

    r = queryExecuteSearch(&ctx->mainDb, &ctx->searchDb, &ctx->stats, symbol, &found, &loc);
    if (r == STATUS_OK)
    {
        (void)printf("\nFound: %s (%s) - $%.2f\n", found.symbol, found.name, found.price);
        (void)printf("Location status: %s\n", location_status_to_string(loc));
    }
    else if (r == STATUS_ERR_INVALID_ARG)
    {
        (void)printf("Invalid symbol format. Symbols must be 1-7 alphanumeric characters.\n");
    }
    else
    {
        (void)printf("Symbol '%s' was not found in the main database.\n", symbol);
        (void)printf("Location status: %s\n", location_status_to_string(LOC_NONE));
    }
}

static void actionViewCacheContents(AppContext *ctx)
{
    Stock *arr = NULL;
    size_t count = 0U;
    status_t r = searchCacheSnapshot(&ctx->searchDb, &arr, &count);

    (void)printf("\n--- YOUR SEARCH CACHE (%s) - last %u searched, most-recent first ---\n",
                 ctx->currentUser.username, (unsigned)SEARCH_CACHE_CAPACITY);
    if ((r == STATUS_OK) && (count > 0U))
    {
        size_t i;
        for (i = 0U; i < count; i++)
        {
            (void)printf("%2zu. %-6s %-30s $%.2f\n", i + 1U, arr[i].symbol, arr[i].name, arr[i].price);
        }
        mmFree(arr);
    }
    else
    {
        (void)printf("(cache is currently empty)\n");
    }
    (void)printf("Cache size: %zu / %u\n", count, (unsigned)SEARCH_CACHE_CAPACITY);
}

static void actionViewAllStocks(AppContext *ctx)
{
    Stock *arr = NULL;
    size_t count = 0U;
    status_t r = mainCacheSnapshot(&ctx->mainDb, &arr, &count);

    (void)printf("\n--- MAIN DATABASE (all stocks) ---\n");
    if ((r == STATUS_OK) && (count > 0U))
    {
        size_t i;
        for (i = 0U; i < count; i++)
        {
            (void)printf("%3zu. %-6s %-30s $%.2f\n", i + 1U, arr[i].symbol, arr[i].name, arr[i].price);
        }
        mmFree(arr);
    }
    (void)printf("Total stocks: %zu\n", count);
}

static void actionCreateAlert(AppContext *ctx, const char *owner)
{
    char symbol[SYMBOL_MAX_LEN];
    status_t r;

    readLine("Symbol for alert: ", symbol, sizeof(symbol));
    if (!secValidateSymbol(symbol))
    {
        (void)printf("Invalid symbol format.\n");
    }
    else
    {
        double threshold = readDouble("Threshold price: ");
        int typeChoice = readIntChoice("Alert type (1 = ABOVE, 2 = BELOW): ");
        alert_type_t type = (typeChoice == 2) ? ALERT_BELOW : ALERT_ABOVE;

        if (!secValidatePrice(threshold))
        {
            (void)printf("Invalid threshold price.\n");
        }
        else
        {
            r = alertsCreate(&ctx->alerts, symbol, threshold, type, owner);
            if (r == STATUS_OK)
            {
                (void)printf("Alert created: %s %s %.2f\n", symbol,
                              (type == ALERT_ABOVE) ? "ABOVE" : "BELOW", threshold);
                (void)loggerLog(LOG_AUDIT, "Alert created by %s: %s %s %.2f", owner, symbol,
                                 (type == ALERT_ABOVE) ? "ABOVE" : "BELOW", threshold);
            }
            else
            {
                (void)printf("Failed to create alert: %s\n", status_to_string(r));
            }
        }
    }
}

static void actionViewAlerts(AppContext *ctx)
{
    Alert all[MAX_ALERTS];
    size_t count = 0U;
    size_t i;

    (void)alertsGetAll(&ctx->alerts, all, MAX_ALERTS, &count);
    (void)printf("\n--- YOUR ALERTS (%s) - %zu total ---\n", ctx->currentUser.username, count);
    for (i = 0U; i < count; i++)
    {
        (void)printf("%3zu. %-6s %s %.2f  [%s]  owner=%s\n", i + 1U, all[i].symbol,
                      (all[i].type == ALERT_ABOVE) ? "ABOVE" : "BELOW", all[i].threshold,
                      all[i].triggered ? "TRIGGERED" : "waiting", all[i].owner);
    }
}

/* ===================== Admin-only actions ================================ */

static void actionAddStock(AppContext *ctx)
{
    Stock st;
    status_t r;

    memset(&st, 0, sizeof(st));
    readLine("New stock symbol: ", st.symbol, sizeof(st.symbol));
    if (!secValidateSymbol(st.symbol))
    {
        (void)printf("Invalid symbol format.\n");
    }
    else
    {
        readLine("Company name: ", st.name, sizeof(st.name));
        if (!secValidateCompanyName(st.name))
        {
            (void)printf("Invalid company name.\n");
        }
        else
        {
            st.price = readDouble("Initial price: ");
            if (!secValidatePrice(st.price))
            {
                (void)printf("Invalid price.\n");
            }
            else
            {
                st.lastUpdated = time(NULL);
                r = mainCacheAdd(&ctx->mainDb, &st);
                if (r == STATUS_OK)
                {
                    (void)printf("Stock %s added to main database.\n", st.symbol);
                    (void)loggerLog(LOG_AUDIT, "Admin added new stock %s (%s) at %.2f",
                                     st.symbol, st.name, st.price);
                }
                else
                {
                    (void)printf("Failed to add stock: %s\n", status_to_string(r));
                }
            }
        }
    }
}

static void actionManualPriceUpdate(AppContext *ctx)
{
    char symbol[SYMBOL_MAX_LEN];
    double newPrice;
    status_t r;
    location_status_t loc;

    readLine("Symbol to update: ", symbol, sizeof(symbol));
    loc = queryLocationStatus(&ctx->mainDb, &ctx->searchDb, symbol);
    (void)printf("Current location status: %s\n", location_status_to_string(loc));

    newPrice = readDouble("New price: ");
    r = feedApplyPriceUpdate(&ctx->mainDb, &ctx->searchDb, &ctx->alerts, &ctx->stats,
                              symbol, newPrice);
    if (r == STATUS_OK)
    {
        (void)printf("Price updated. Main DB updated");
        if (loc == LOC_BOTH || loc == LOC_CACHE_ONLY)
        {
            (void)printf(" and cache entry synced");
        }
        (void)printf(".\n");
    }
    else
    {
        (void)printf("Update failed: %s\n", status_to_string(r));
    }
}

static void actionViewStats(AppContext *ctx)
{
    StatsSnapshot snap;
    (void)statsGetSnapshot(&ctx->stats, &snap);
    (void)printf("\n--- STATISTICS ---\n");
    (void)printf("Searches performed : %lu\n", snap.searchCount);
    (void)printf("Price updates      : %lu\n", snap.updateCount);
    (void)printf("Cache hits         : %lu\n", snap.cacheHits);
    (void)printf("Cache misses       : %lu\n", snap.cacheMisses);
    (void)printf("Cache hit ratio    : %.2f%%\n", snap.hitRatio * 100.0);
}

static void testerSubmenu(void)
{
    bool exitSubmenu = false;

    while (!exitSubmenu)
    {
        int choice;
        (void)printf("\n===== TESTER MODULE =====\n");
        (void)printf("1. Unit Testing\n");
        (void)printf("2. Integration Testing\n");
        (void)printf("3. Memory Leak Testing\n");
        (void)printf("4. Safety and Security Validation Testing\n");
        (void)printf("5. Smart Testing (run all 4 above)\n");
        (void)printf("6. Benchmark: Hash Table vs Linear Search\n");
        (void)printf("7. Benchmark: rwlock vs mutex (concurrency)\n");
        (void)printf("8. Demo: LRU cache vs plain FIFO cache\n");
        (void)printf("0. Back\n");
        choice = readIntChoice("Choice: ");

        if (!secValidateMenuChoice(choice, 0, 8))
        {
            (void)printf("Invalid choice.\n");
        }
        else
        {
            switch (choice)
            {
                case 1: (void)testerRunUnitTests(); break;
                case 2: (void)testerRunIntegrationTests(); break;
                case 3: (void)testerRunMemoryLeakTest(); break;
                case 4: (void)testerRunSecurityValidationTests(); break;
                case 5: (void)testerRunSmartTesting(); break;
                case 6: testerRunBenchmarkHashVsLinear(); break;
                case 7: testerRunBenchmarkRwlockVsMutex(); break;
                case 8: testerRunDemoLruVsFifo(); break;
                case 0: exitSubmenu = true; break;
                default: break;
            }
            if (!exitSubmenu)
            {
                pauseForUser();
            }
        }
    }
}

static void adminMenu(AppContext *ctx)
{
    bool loggedOut = false;

    while (!loggedOut)
    {
        int choice;
        (void)printf("\n===== ADMIN MENU (%s) =====\n", ctx->currentUser.username);
        (void)printf(" 1. Search Stock\n");
        (void)printf(" 2. View My Cache (last %u searched)\n", (unsigned)SEARCH_CACHE_CAPACITY);
        (void)printf(" 3. View All Stocks (main DB)\n");
        (void)printf(" 4. Add New Stock\n");
        (void)printf(" 5. Manual Price Update\n");
        (void)printf(" 6. Create Price Alert\n");
        (void)printf(" 7. View My Alerts\n");
        (void)printf(" 8. View Statistics\n");
        (void)printf(" 9. Diagnostics (memory / performance)\n");
        (void)printf("10. Optimizer Reports (optimization / security / health)\n");
        (void)printf("11. Save All Databases Now\n");
        (void)printf(" 0. Logout\n");
        choice = readIntChoice("Choice: ");

        if (!secValidateMenuChoice(choice, 0, 11))
        {
            (void)printf("Invalid choice.\n");
        }
        else
        {
            switch (choice)
            {
                case 1: actionSearchStock(ctx); break;
                case 2: actionViewCacheContents(ctx); break;
                case 3: actionViewAllStocks(ctx); break;
                case 4: actionAddStock(ctx); break;
                case 5: actionManualPriceUpdate(ctx); break;
                case 6: actionCreateAlert(ctx, ctx->currentUser.username); break;
                case 7: actionViewAlerts(ctx); break;
                case 8: actionViewStats(ctx); break;
                case 9:
                    (void)diagPrintMemoryReport();
                    (void)diagPrintPerformanceReport(&ctx->mainDb, &ctx->searchDb);
                    break;
                case 10:
                    (void)optimizerPrintOptimizationReport(&ctx->mainDb, &ctx->searchDb, &ctx->stats);
                    (void)optimizerPrintSecurityReport(&ctx->alerts);
                    (void)optimizerPrintSystemHealthReport(&ctx->mainDb, &ctx->searchDb);
                    break;
                case 11: persistAll(ctx); (void)printf("Saved.\n"); break;
                case 0: loggedOut = true; break;
                default: break;
            }
            if (!loggedOut)
            {
                pauseForUser();
            }
        }
    }
}

static void userMenu(AppContext *ctx)
{
    bool loggedOut = false;

    while (!loggedOut)
    {
        int choice;
        (void)printf("\n===== USER MENU (%s) =====\n", ctx->currentUser.username);
        (void)printf("1. Search Stock\n");
        (void)printf("2. View My Cache (last %u searched)\n", (unsigned)SEARCH_CACHE_CAPACITY);
        (void)printf("3. View All Stocks (main DB)\n");
        (void)printf("4. Create Price Alert\n");
        (void)printf("5. View My Alerts\n");
        (void)printf("0. Logout\n");
        choice = readIntChoice("Choice: ");

        if (!secValidateMenuChoice(choice, 0, 5))
        {
            (void)printf("Invalid choice.\n");
        }
        else
        {
            switch (choice)
            {
                case 1: actionSearchStock(ctx); break;
                case 2: actionViewCacheContents(ctx); break;
                case 3: actionViewAllStocks(ctx); break;
                case 4: actionCreateAlert(ctx, ctx->currentUser.username); break;
                case 5: actionViewAlerts(ctx); break;
                case 0: loggedOut = true; break;
                default: break;
            }
            if (!loggedOut)
            {
                pauseForUser();
            }
        }
    }
}

/* The TESTER role is intentionally the most restricted menu in the
 * app: a QA/testing account should be able to run the self-test suite
 * without also holding the power to edit stocks, change prices, or
 * touch anything else an ADMIN can. This is a least-privilege
 * separation, distinct from both ADMIN and USER. */
static void testerOnlyMenu(AppContext *ctx)
{
    bool loggedOut = false;

    while (!loggedOut)
    {
        int choice;
        (void)printf("\n===== TESTER ACCOUNT MENU (%s) =====\n", ctx->currentUser.username);
        (void)printf("1. Open Tester Module (run self-tests)\n");
        (void)printf("0. Logout\n");
        choice = readIntChoice("Choice: ");

        if (!secValidateMenuChoice(choice, 0, 1))
        {
            (void)printf("Invalid choice.\n");
        }
        else
        {
            switch (choice)
            {
                case 1: testerSubmenu(); break;
                case 0: loggedOut = true; break;
                default: break;
            }
        }
    }
}

static bool loginFlow(AppContext *ctx)
{
    char username[USERNAME_MAX_LEN];
    char password[PASSWORD_MAX_LEN];
    status_t r;
    bool success = false;

    (void)printf("\n===== LOGIN =====\n");
    readLine("Username: ", username, sizeof(username));
    readLine("Password: ", password, sizeof(password));

    r = authLogin(&ctx->users, username, password, &ctx->currentUser);
    if (r == STATUS_OK)
    {
        (void)printf("Welcome, %s (%s)!\n", ctx->currentUser.username,
                      authIsAdmin(&ctx->currentUser) ? "ADMIN" :
                      (authIsTester(&ctx->currentUser) ? "TESTER" : "USER"));
        success = true;
    }
    else
    {
        (void)printf("Login failed. Check username/password.\n");
        success = false;
    }
    return success;
}

int main(void)
{
    AppContext ctx;
    bool running = true;

    (void)memset(&ctx, 0, sizeof(ctx));
    (void)bootstrap(&ctx);

    (void)printf("=============================================\n");
    (void)printf(" Real-Time Stock Market Data Cache System\n");
    (void)printf("=============================================\n");

    while (running)
    {
        if (loginFlow(&ctx))
        {
            /* Stop background threads before swapping in this user's own
             * cache/alerts: loadUserSession() destroys and re-initializes
             * those structures (including their internal locks), which
             * would race with the feed thread if it were still running. */
            (void)tmStopAll(&ctx.tm);
            loadUserSession(&ctx);
            ctx.hasLoggedInUser = true;
            (void)tmStartAll(&ctx.tm);

            if (authIsAdmin(&ctx.currentUser))
            {
                adminMenu(&ctx);
            }
            else if (authIsTester(&ctx.currentUser))
            {
                testerOnlyMenu(&ctx);
            }
            else
            {
                userMenu(&ctx);
            }

            /* Safe to call with threads still running: the underlying
             * save functions lock the same mutexes the threads use. */
            saveUserSession(&ctx);
        }

        {
            int choice = readIntChoice("\nLogin again (1) or Exit (0)? ");
            if (choice != 1)
            {
                running = false;
            }
        }
    }

    (void)printf("Saving databases and shutting down background threads...\n");
    appShutdown(&ctx);
    (void)printf("Goodbye.\n");

    return 0;
}
