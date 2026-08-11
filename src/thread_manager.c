#include "thread_manager.h"
#include "feed.h"
#include "logger.h"
#include <time.h>
#include <errno.h>

#define DEFAULT_FEED_INTERVAL_SEC      3U
#define DEFAULT_HEARTBEAT_INTERVAL_SEC 10U
#define DEFAULT_STATS_INTERVAL_SEC     15U

/* Sleeps up to seconds, but wakes immediately if tmStopAll() is called.
 * Returns 1 if a stop was requested, 0 if the timeout elapsed normally. */
static int interruptibleSleep(ThreadManager *tm, unsigned int seconds)
{
    int stopped = 0;
    struct timespec ts;

    (void)clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (time_t)seconds;

    (void)pthread_mutex_lock(&tm->stateLock);
    while (tm->running == 1)
    {
        int rc = pthread_cond_timedwait(&tm->stateCond, &tm->stateLock, &ts);
        if (rc == ETIMEDOUT)
        {
            break;
        }
        /* Woken by signal (stop requested) - loop re-checks tm->running */
    }
    stopped = (tm->running == 0) ? 1 : 0;
    (void)pthread_mutex_unlock(&tm->stateLock);

    return stopped;
}

static void *feedThreadFunc(void *arg)
{
    ThreadManager *tm = (ThreadManager *)arg;
    for (;;)
    {
        char symbol[SYMBOL_MAX_LEN];
        double price = 0.0;
        status_t r;

        if (interruptibleSleep(tm, tm->feedIntervalSeconds) == 1)
        {
            break;
        }

        r = feedSimulateTick(tm->mainDb, tm->searchDb, tm->alerts, tm->stats,
                              symbol, sizeof(symbol), &price);
        if (r == STATUS_OK)
        {
            (void)loggerLog(LOG_HISTORY, "FEED TICK symbol=%s price=%.4f", symbol, price);
        }
    }
    return NULL;
}

static void *loggerThreadFunc(void *arg)
{
    ThreadManager *tm = (ThreadManager *)arg;
    for (;;)
    {
        if (interruptibleSleep(tm, tm->heartbeatIntervalSeconds) == 1)
        {
            break;
        }
        (void)loggerLog(LOG_AUDIT, "HEARTBEAT logger thread alive");
    }
    return NULL;
}

static void *statsThreadFunc(void *arg)
{
    ThreadManager *tm = (ThreadManager *)arg;
    for (;;)
    {
        StatsSnapshot snap;
        if (interruptibleSleep(tm, tm->statsIntervalSeconds) == 1)
        {
            break;
        }
        if (statsGetSnapshot(tm->stats, &snap) == STATUS_OK)
        {
            (void)loggerLog(LOG_HISTORY,
                "STATS SNAPSHOT searches=%lu updates=%lu hits=%lu misses=%lu hitRatio=%.4f",
                snap.searchCount, snap.updateCount, snap.cacheHits, snap.cacheMisses,
                snap.hitRatio);
        }
    }
    return NULL;
}

status_t tmInit(ThreadManager *tm, MainCache *mainDb, SearchCache *searchDb,
                 AlertStore *alerts, Stats *stats)
{
    status_t result = STATUS_OK;

    if ((tm == NULL) || (mainDb == NULL) || (searchDb == NULL) ||
        (alerts == NULL) || (stats == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        tm->mainDb = mainDb;
        tm->searchDb = searchDb;
        tm->alerts = alerts;
        tm->stats = stats;
        tm->running = 0;
        tm->feedIntervalSeconds = DEFAULT_FEED_INTERVAL_SEC;
        tm->heartbeatIntervalSeconds = DEFAULT_HEARTBEAT_INTERVAL_SEC;
        tm->statsIntervalSeconds = DEFAULT_STATS_INTERVAL_SEC;
        tm->feedStarted = 0;
        tm->loggerStarted = 0;
        tm->statsStarted = 0;

        if (pthread_mutex_init(&tm->stateLock, NULL) != 0)
        {
            result = STATUS_ERR_LOCK;
        }
        else if (pthread_cond_init(&tm->stateCond, NULL) != 0)
        {
            result = STATUS_ERR_LOCK;
        }
        else
        {
            /* nothing further to do */
        }
    }
    return result;
}

status_t tmStartAll(ThreadManager *tm)
{
    status_t result = STATUS_OK;

    if (tm == NULL)
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        (void)pthread_mutex_lock(&tm->stateLock);
        tm->running = 1;
        tm->feedStarted = 0;
        tm->loggerStarted = 0;
        tm->statsStarted = 0;
        (void)pthread_mutex_unlock(&tm->stateLock);

        /* Create feed thread */
        if (pthread_create(&tm->feedThread, NULL, feedThreadFunc, tm) != 0)
        {
            result = STATUS_ERR_UNKNOWN;
        }
        else
        {
            tm->feedStarted = 1;
        }

        /* Create logger thread */
        if (result == STATUS_OK)
        {
            if (pthread_create(&tm->loggerThread, NULL, loggerThreadFunc, tm) != 0)
            {
                result = STATUS_ERR_UNKNOWN;
            }
            else
            {
                tm->loggerStarted = 1;
            }
        }

        /* Create stats thread */
        if (result == STATUS_OK)
        {
            if (pthread_create(&tm->statsThread, NULL, statsThreadFunc, tm) != 0)
            {
                result = STATUS_ERR_UNKNOWN;
            }
            else
            {
                tm->statsStarted = 1;
            }
        }

        if (result == STATUS_OK)
        {
            (void)loggerLog(LOG_AUDIT, "Background threads started (feed/logger/stats)");
        }
        else
        {
            /* Partial failure: ensure any started threads are signalled to stop and joined. */
            (void)pthread_mutex_lock(&tm->stateLock);
            tm->running = 0;
            (void)pthread_cond_broadcast(&tm->stateCond);
            (void)pthread_mutex_unlock(&tm->stateLock);

            if (tm->feedStarted)  { (void)pthread_join(tm->feedThread, NULL); tm->feedStarted = 0; }
            if (tm->loggerStarted){ (void)pthread_join(tm->loggerThread, NULL); tm->loggerStarted = 0; }
            if (tm->statsStarted) { (void)pthread_join(tm->statsThread, NULL); tm->statsStarted = 0; }
        }
    }
    return result;
}

status_t tmStopAll(ThreadManager *tm)
{
    status_t result = STATUS_OK;

    if (tm == NULL)
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        (void)pthread_mutex_lock(&tm->stateLock);
        tm->running = 0;
        (void)pthread_cond_broadcast(&tm->stateCond);
        (void)pthread_mutex_unlock(&tm->stateLock);

        if (tm->feedStarted)
        {
            (void)pthread_join(tm->feedThread, NULL);
            tm->feedStarted = 0;
        }
        if (tm->loggerStarted)
        {
            (void)pthread_join(tm->loggerThread, NULL);
            tm->loggerStarted = 0;
        }
        if (tm->statsStarted)
        {
            (void)pthread_join(tm->statsThread, NULL);
            tm->statsStarted = 0;
        }

        (void)loggerLog(LOG_AUDIT, "Background threads stopped cleanly");
    }
    return result;
}

status_t tmDestroy(ThreadManager *tm)
{
    status_t result = STATUS_OK;
    if (tm == NULL)
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        (void)pthread_mutex_destroy(&tm->stateLock);
        (void)pthread_cond_destroy(&tm->stateCond);
    }
    return result;
}
