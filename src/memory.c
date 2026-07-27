#include "memory.h"
#include <stdlib.h>
#include "portable_pthread.h"

/* Each tracked block is prefixed with this header so mmFree knows the size */
typedef struct {
    size_t size;
    unsigned long magic;
} BlockHeader;

#define MM_MAGIC 0x4D4D4831UL /* "MMH1" */

static pthread_mutex_t g_mmLock;
static MemStats g_stats = {0UL, 0UL, 0L, 0U};
static int g_initialized = 0;

status_t mmInit(void)
{
    status_t result = STATUS_OK;
    (void)pthread_mutex_init(&g_mmLock, NULL);
    (void)pthread_mutex_lock(&g_mmLock);
    g_stats.totalAllocations = 0UL;
    g_stats.totalFrees = 0UL;
    g_stats.activeNodes = 0L;
    g_stats.bytesCurrentlyAllocated = 0U;
    g_initialized = 1;
    (void)pthread_mutex_unlock(&g_mmLock);
    return result;
}

status_t mmShutdown(void)
{
    (void)pthread_mutex_lock(&g_mmLock);
    g_initialized = 0;
    (void)pthread_mutex_unlock(&g_mmLock);
    return STATUS_OK;
}

void *mmAlloc(size_t size)
{
    void *userPtr = NULL;

    if (size > 0U)
    {
        BlockHeader *block = (BlockHeader *)malloc(sizeof(BlockHeader) + size);
        if (block != NULL)
        {
            block->size = size;
            block->magic = MM_MAGIC;
            userPtr = (void *)(block + 1);

            (void)pthread_mutex_lock(&g_mmLock);
            g_stats.totalAllocations++;
            g_stats.activeNodes++;
            g_stats.bytesCurrentlyAllocated += size;
            (void)pthread_mutex_unlock(&g_mmLock);
        }
    }
    return userPtr;
}

void mmFree(void *ptr)
{
    if (ptr != NULL)
    {
        BlockHeader *block = ((BlockHeader *)ptr) - 1;
        if (block->magic == MM_MAGIC)
        {
            (void)pthread_mutex_lock(&g_mmLock);
            g_stats.totalFrees++;
            g_stats.activeNodes--;
            if (g_stats.bytesCurrentlyAllocated >= block->size)
            {
                g_stats.bytesCurrentlyAllocated -= block->size;
            }
            (void)pthread_mutex_unlock(&g_mmLock);
            block->magic = 0UL;
            free(block);
        }
        else
        {
            /* Not one of ours; free defensively as-is to avoid leaking */
            free(ptr);
        }
    }
}

status_t mmGetStats(MemStats *outStats)
{
    status_t result = STATUS_OK;
    if (outStats == NULL)
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        (void)pthread_mutex_lock(&g_mmLock);
        *outStats = g_stats;
        (void)pthread_mutex_unlock(&g_mmLock);
    }
    return result;
}
