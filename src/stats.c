#include "stats.h"

status_t statsInit(Stats *stats)
{
    status_t result = STATUS_OK;
    if (stats == NULL)
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        stats->searchCount = 0UL;
        stats->updateCount = 0UL;
        stats->cacheHits = 0UL;
        stats->cacheMisses = 0UL;
        if (pthread_mutex_init(&stats->lock, NULL) != 0)
        {
            result = STATUS_ERR_LOCK;
        }
    }
    return result;
}

status_t statsDestroy(Stats *stats)
{
    status_t result = STATUS_OK;
    if (stats == NULL)
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        (void)pthread_mutex_destroy(&stats->lock);
    }
    return result;
}

void statsRecordSearch(Stats *stats)
{
    if (stats != NULL)
    {
        (void)pthread_mutex_lock(&stats->lock);
        stats->searchCount++;
        (void)pthread_mutex_unlock(&stats->lock);
    }
}

void statsRecordUpdate(Stats *stats)
{
    if (stats != NULL)
    {
        (void)pthread_mutex_lock(&stats->lock);
        stats->updateCount++;
        (void)pthread_mutex_unlock(&stats->lock);
    }
}

void statsRecordCacheHit(Stats *stats)
{
    if (stats != NULL)
    {
        (void)pthread_mutex_lock(&stats->lock);
        stats->cacheHits++;
        (void)pthread_mutex_unlock(&stats->lock);
    }
}

void statsRecordCacheMiss(Stats *stats)
{
    if (stats != NULL)
    {
        (void)pthread_mutex_lock(&stats->lock);
        stats->cacheMisses++;
        (void)pthread_mutex_unlock(&stats->lock);
    }
}

double statsGetHitRatio(Stats *stats)
{
    double ratio = 0.0;
    if (stats != NULL)
    {
        (void)pthread_mutex_lock(&stats->lock);
        {
            unsigned long total = stats->cacheHits + stats->cacheMisses;
            if (total > 0UL)
            {
                ratio = (double)stats->cacheHits / (double)total;
            }
        }
        (void)pthread_mutex_unlock(&stats->lock);
    }
    return ratio;
}

status_t statsGetSnapshot(Stats *stats, StatsSnapshot *outSnap)
{
    status_t result = STATUS_OK;
    if ((stats == NULL) || (outSnap == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        (void)pthread_mutex_lock(&stats->lock);
        {
            unsigned long total = stats->cacheHits + stats->cacheMisses;
            outSnap->searchCount = stats->searchCount;
            outSnap->updateCount = stats->updateCount;
            outSnap->cacheHits = stats->cacheHits;
            outSnap->cacheMisses = stats->cacheMisses;
            outSnap->hitRatio = (total > 0UL) ? ((double)stats->cacheHits / (double)total) : 0.0;
        }
        (void)pthread_mutex_unlock(&stats->lock);
    }
    return result;
}
