#ifndef COMMON_H
#define COMMON_H

/* Needed under strict -std=c11 to expose POSIX APIs (pthread_rwlock,
 * strnlen, etc.) used throughout this project on Linux/Mac. Has no
 * effect on Windows, where threading instead goes through
 * portable_pthread.h. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* ===================== Named constants (no magic numbers) ============ */
#define SYMBOL_MAX_LEN        8U        /* e.g. "GOOGL" + NUL          */
#define NAME_MAX_LEN           64U
#define USERNAME_MAX_LEN       32U
#define PASSWORD_MAX_LEN       32U
#define LINE_MAX_LEN           256U
#define PATH_MAX_LEN           256U
#define MAIN_DB_BUCKETS        101U     /* prime bucket count          */
#define SEARCH_CACHE_CAPACITY  10U
#define SEED_STOCK_COUNT       100U
#define MAX_ALERTS             256U
#define MAX_USERS              32U
#define MIN_PRICE              0.01
#define MAX_PRICE              1000000.0
#define MENU_MIN_CHOICE        0
#define MENU_MAX_CHOICE        99

#define DEFAULT_STOCK_DB_PATH   "data/stock.db"
#define DEFAULT_CACHE_DB_PATH   "data/cache.db"
#define DEFAULT_ALERTS_DB_PATH  "data/alerts.db"
#define DEFAULT_USERS_PATH      "data/users.txt"

#define ACCESS_LOG_PATH  "logs/access.log"
#define AUDIT_LOG_PATH   "logs/audit.log"
#define ERROR_LOG_PATH   "logs/error.log"
#define HISTORY_LOG_PATH "logs/history.log"

/* ===================== Status codes ==================================== */
typedef enum {
    STATUS_OK = 0,
    STATUS_ERR_NOT_FOUND = 1,
    STATUS_ERR_INVALID_ARG = 2,
    STATUS_ERR_IO = 3,
    STATUS_ERR_FULL = 4,
    STATUS_ERR_DUPLICATE = 5,
    STATUS_ERR_MEMORY = 6,
    STATUS_ERR_AUTH = 7,
    STATUS_ERR_PERMISSION = 8,
    STATUS_ERR_LOCK = 9,
    STATUS_ERR_UNKNOWN = 10
} status_t;

/* ===================== Roles ============================================ */
typedef enum {
    ROLE_USER = 0,
    ROLE_ADMIN = 1,
    ROLE_TESTER = 2 /* dedicated QA account: can ONLY run the Tester
                      * Module (in-app self-tests) - no stock edits, no
                      * price updates, no other admin-only actions. */
} role_t;

/* ===================== Core domain types ================================ */
typedef struct {
    char   symbol[SYMBOL_MAX_LEN];
    char   name[NAME_MAX_LEN];
    double price;
    time_t lastUpdated;
} Stock;

typedef struct {
    char username[USERNAME_MAX_LEN];
    char password[PASSWORD_MAX_LEN];
    role_t role;
} User;

/* Location status of a symbol relative to the two databases */
typedef enum {
    LOC_NONE = 0,
    LOC_MAIN_ONLY = 1,
    LOC_CACHE_ONLY = 2,
    LOC_BOTH = 3
} location_status_t;

/* ===================== Bounds-checked string helpers ==================== */
/* Returns STATUS_OK or STATUS_ERR_INVALID_ARG. Always NUL-terminates dst
 * within dstSize (never overflows), unlike raw strcpy/strcat. */
status_t safe_strcpy(char *dst, size_t dstSize, const char *src);
status_t safe_strcat(char *dst, size_t dstSize, const char *src);

/* Case-insensitive, bounded compare of two symbol-like strings */
int safe_strcasecmp(const char *a, const char *b);

/* Portable, dependency-free replacement for strnlen() - written by hand
 * so this project never depends on strnlen actually being present
 * (it's a security extension, not guaranteed on every toolchain). */
size_t portable_strnlen(const char *s, size_t maxLen);

const char *status_to_string(status_t s);
const char *location_status_to_string(location_status_t l);

#endif /* COMMON_H */
