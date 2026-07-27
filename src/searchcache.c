#include "searchcache.h"
#include "memory.h"
#include <string.h>

static void detachNode(SearchCache *sc, SearchCacheNode *node)
{
    if (node->prev != NULL)
    {
        node->prev->next = node->next;
    }
    else
    {
        sc->head = node->next;
    }

    if (node->next != NULL)
    {
        node->next->prev = node->prev;
    }
    else
    {
        sc->tail = node->prev;
    }

    node->prev = NULL;
    node->next = NULL;
}

static void pushFront(SearchCache *sc, SearchCacheNode *node)
{
    node->prev = NULL;
    node->next = sc->head;
    if (sc->head != NULL)
    {
        sc->head->prev = node;
    }
    sc->head = node;
    if (sc->tail == NULL)
    {
        sc->tail = node;
    }
}

status_t searchCacheInit(SearchCache *sc)
{
    status_t result = STATUS_OK;
    if (sc == NULL)
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        sc->head = NULL;
        sc->tail = NULL;
        sc->count = 0U;
        if (pthread_mutex_init(&sc->lock, NULL) != 0)
        {
            result = STATUS_ERR_LOCK;
        }
    }
    return result;
}

status_t searchCacheDestroy(SearchCache *sc)
{
    status_t result = STATUS_OK;
    if (sc == NULL)
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        (void)pthread_mutex_lock(&sc->lock);
        {
            SearchCacheNode *node = sc->head;
            while (node != NULL)
            {
                SearchCacheNode *next = node->next;
                mmFree(node);
                node = next;
            }
            sc->head = NULL;
            sc->tail = NULL;
            sc->count = 0U;
        }
        (void)pthread_mutex_unlock(&sc->lock);
        (void)pthread_mutex_destroy(&sc->lock);
    }
    return result;
}

status_t searchCacheTouch(SearchCache *sc, const Stock *stock)
{
    status_t result = STATUS_OK;

    if ((sc == NULL) || (stock == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        (void)pthread_mutex_lock(&sc->lock);
        {
            SearchCacheNode *node = sc->head;
            SearchCacheNode *existing = NULL;

            while (node != NULL)
            {
                if (safe_strcasecmp(node->data.symbol, stock->symbol) == 0)
                {
                    existing = node;
                    break;
                }
                node = node->next;
            }

            if (existing != NULL)
            {
                existing->data = *stock;
                detachNode(sc, existing);
                pushFront(sc, existing);
            }
            else
            {
                if (sc->count >= SEARCH_CACHE_CAPACITY)
                {
                    SearchCacheNode *lru = sc->tail;
                    if (lru != NULL)
                    {
                        detachNode(sc, lru);
                        mmFree(lru);
                        sc->count--;
                    }
                }

                {
                    SearchCacheNode *newNode = (SearchCacheNode *)mmAlloc(sizeof(SearchCacheNode));
                    if (newNode == NULL)
                    {
                        result = STATUS_ERR_MEMORY;
                    }
                    else
                    {
                        newNode->data = *stock;
                        newNode->prev = NULL;
                        newNode->next = NULL;
                        pushFront(sc, newNode);
                        sc->count++;
                    }
                }
            }
        }
        (void)pthread_mutex_unlock(&sc->lock);
    }
    return result;
}

status_t searchCacheSearch(SearchCache *sc, const char *symbol, Stock *outStock)
{
    status_t result = STATUS_ERR_NOT_FOUND;

    if ((sc == NULL) || (symbol == NULL) || (outStock == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        (void)pthread_mutex_lock(&sc->lock);
        {
            SearchCacheNode *node = sc->head;
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
        (void)pthread_mutex_unlock(&sc->lock);
    }
    return result;
}

status_t searchCacheUpdatePrice(SearchCache *sc, const char *symbol, double newPrice)
{
    status_t result = STATUS_ERR_NOT_FOUND;

    if ((sc == NULL) || (symbol == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        (void)pthread_mutex_lock(&sc->lock);
        {
            SearchCacheNode *node = sc->head;
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
        (void)pthread_mutex_unlock(&sc->lock);
    }
    return result;
}

bool searchCacheContains(SearchCache *sc, const char *symbol)
{
    Stock tmp;
    return (searchCacheSearch(sc, symbol, &tmp) == STATUS_OK);
}

size_t searchCacheCount(SearchCache *sc)
{
    size_t count = 0U;
    if (sc != NULL)
    {
        (void)pthread_mutex_lock(&sc->lock);
        count = sc->count;
        (void)pthread_mutex_unlock(&sc->lock);
    }
    return count;
}

status_t searchCacheSnapshot(SearchCache *sc, Stock **outArray, size_t *outCount)
{
    status_t result = STATUS_OK;

    if ((sc == NULL) || (outArray == NULL) || (outCount == NULL))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        (void)pthread_mutex_lock(&sc->lock);
        {
            size_t total = sc->count;
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
                    size_t i = 0U;
                    SearchCacheNode *node = sc->head;
                    while ((node != NULL) && (i < total))
                    {
                        arr[i] = node->data;
                        i++;
                        node = node->next;
                    }
                    *outArray = arr;
                    *outCount = i;
                }
            }
        }
        (void)pthread_mutex_unlock(&sc->lock);
    }
    return result;
}
