#include "cache.h"
#include "memory.h"
#include <string.h>
#include <ctype.h>

unsigned long mainCacheHash(const char *symbol)
{
    unsigned long hash = 5381UL;

    if (symbol != NULL)
    {
        size_t i = 0U;
        while (symbol[i] != '\0')
        {
            /* Hash in a case-insensitive manner to match lookups that use
             * case-insensitive compares elsewhere in the codebase. */
            unsigned char c = (unsigned char)symbol[i];
            unsigned char lc = (unsigned char)tolower(c);
            hash = ((hash << 5) + hash) + (unsigned long)lc;
            i++;
        }
    }
    return (hash % MAIN_DB_BUCKETS);
}

status_t mainCacheInit(MainCache *cache)
{
    status_t result = STATUS_OK;

    if (cache == NULL)
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        size_t i;
        for (i = 0U; i < MAIN_DB_BUCKETS; i++)
        {
            cache->buckets[i] = NULL;
        }
        cache->count = 0U;
        if (pthread_rwlock_init(&cache->lock, NULL) != 0)
        {
            result = STATUS_ERR_LOCK;
        }
    }
    return result;
}

status_t mainCacheDestroy(MainCache *cache)
{
    status_t result = STATUS_OK;

    if (cache == NULL)
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        size_t i;
        (void)pthread_rwlock_wrlock(&cache->lock);
        for (i = 0U; i < MAIN_DB_BUCKETS; i++)
        {
            MainCacheNode *node = cache->buckets[i];
            while (node != NULL)
            {
                MainCacheNode *next = node->next;
                mmFree(node);
                node = next;
            }
            cache->buckets[i] = NULL;
        }
        cache->count = 0U;
        (void)pthread_rwlock_unlock(&cache->lock);
        (void)pthread_rwlock_destroy(&cache->lock);
    }
    return result;
}

status_t mainCacheAdd(MainCache *cache, const Stock *stock)
{
    status_t result = STATUS_OK;

    if ((cache == NULL) || (stock == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        unsigned long idx = mainCacheHash(stock->symbol);
        (void)pthread_rwlock_wrlock(&cache->lock);
        {
            MainCacheNode *node = cache->buckets[idx];
            bool found = false;
            while (node != NULL)
            {
                if (safe_strcasecmp(node->data.symbol, stock->symbol) == 0)
                {
                    found = true;
                    break;
                }
                node = node->next;
            }

            if (found)
            {
                result = STATUS_ERR_DUPLICATE;
            }
            else
            {
                MainCacheNode *newNode = (MainCacheNode *)mmAlloc(sizeof(MainCacheNode));
                if (newNode == NULL)
                {
                    result = STATUS_ERR_MEMORY;
                }
                else
                {
                    newNode->data = *stock;
                    newNode->next = cache->buckets[idx];
                    cache->buckets[idx] = newNode;
                    cache->count++;
                }
            }
        }
        (void)pthread_rwlock_unlock(&cache->lock);
    }
    return result;
}

status_t mainCacheSearch(MainCache *cache, const char *symbol, Stock *outStock)
{
    status_t result = STATUS_ERR_NOT_FOUND;

    if ((cache == NULL) || (symbol == NULL) || (outStock == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        unsigned long idx = mainCacheHash(symbol);
        (void)pthread_rwlock_rdlock(&cache->lock);
        {
            MainCacheNode *node = cache->buckets[idx];
            while (node != NULL)
            {
                if (safe_strcasecmp(node->data.symbol, symbol) == 0)
                {
                    *outStock = node->data;
                    result = STATUS_OK;
                    break;
                }
                node = node->next;
            }
        }
        (void)pthread_rwlock_unlock(&cache->lock);
    }
    return result;
}

status_t mainCacheUpdatePrice(MainCache *cache, const char *symbol, double newPrice)
{
    status_t result = STATUS_ERR_NOT_FOUND;

    if ((cache == NULL) || (symbol == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        unsigned long idx = mainCacheHash(symbol);
        (void)pthread_rwlock_wrlock(&cache->lock);
        {
            MainCacheNode *node = cache->buckets[idx];
            while (node != NULL)
            {
                if (safe_strcasecmp(node->data.symbol, symbol) == 0)
                {
                    node->data.price = newPrice;
                    node->data.lastUpdated = time(NULL);
                    result = STATUS_OK;
                    break;
                }
                node = node->next;
            }
        }
        (void)pthread_rwlock_unlock(&cache->lock);
    }
    return result;
}

status_t mainCacheDelete(MainCache *cache, const char *symbol)
{
    status_t result = STATUS_ERR_NOT_FOUND;

    if ((cache == NULL) || (symbol == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        unsigned long idx = mainCacheHash(symbol);
        (void)pthread_rwlock_wrlock(&cache->lock);
        {
            MainCacheNode *node = cache->buckets[idx];
            MainCacheNode *prev = NULL;
            while (node != NULL)
            {
                if (safe_strcasecmp(node->data.symbol, symbol) == 0)
                {
                    if (prev == NULL)
                    {
                        cache->buckets[idx] = node->next;
                    }
                    else
                    {
                        prev->next = node->next;
                    }
                    mmFree(node);
                    cache->count--;
                    result = STATUS_OK;
                    break;
                }
                prev = node;
                node = node->next;
            }
        }
        (void)pthread_rwlock_unlock(&cache->lock);
    }
    return result;
}

bool mainCacheContains(MainCache *cache, const char *symbol)
{
    Stock tmp;
    return (mainCacheSearch(cache, symbol, &tmp) == STATUS_OK);
}

size_t mainCacheCount(MainCache *cache)
{
    size_t count = 0U;
    if (cache != NULL)
    {
        (void)pthread_rwlock_rdlock(&cache->lock);
        count = cache->count;
        (void)pthread_rwlock_unlock(&cache->lock);
    }
    return count;
}

status_t mainCacheSnapshot(MainCache *cache, Stock **outArray, size_t *outCount)
{
    status_t result = STATUS_OK;

    if ((cache == NULL) || (outArray == NULL) || (outCount == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        (void)pthread_rwlock_rdlock(&cache->lock);
        {
            size_t total = cache->count;
            if (total == 0U)
            {
                *outArray = NULL;
                *outCount = 0U;
            }
            else
            {
                Stock *arr = (Stock *)mmAlloc(sizeof(Stock) * total);
                if (arr == NULL)
                {
                    result = STATUS_ERR_MEMORY;
                }
                else
                {
                    size_t writeIdx = 0U;
                    size_t i;
                    for (i = 0U; i < MAIN_DB_BUCKETS; i++)
                    {
                        MainCacheNode *node = cache->buckets[i];
                        while ((node != NULL) && (writeIdx < total))
                        {
                            arr[writeIdx] = node->data;
                            writeIdx++;
                            node = node->next;
                        }
                    }
                    *outArray = arr;
                    *outCount = writeIdx;
                }
            }
        }
        (void)pthread_rwlock_unlock(&cache->lock);
    }
    return result;
}
