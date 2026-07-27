#ifndef MEMORY_H
#define MEMORY_H

#include <stddef.h>
#include "common.h"

typedef struct {
    unsigned long totalAllocations;
    unsigned long totalFrees;
    long           activeNodes;
    size_t         bytesCurrentlyAllocated;
} MemStats;

/* Must be called once before any mmAlloc/mmFree use, and mmShutdown at exit */
status_t mmInit(void);
status_t mmShutdown(void);

/* Thread-safe tracked allocation. Returns NULL on failure. */
void *mmAlloc(size_t size);
/* Thread-safe tracked free. Safe to call with NULL. */
void  mmFree(void *ptr);

status_t mmGetStats(MemStats *outStats);

#endif /* MEMORY_H */
