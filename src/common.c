#include "common.h"
#include <string.h>
#include <ctype.h>

size_t portable_strnlen(const char *s, size_t maxLen)
{
    size_t len = 0U;
    if (s != NULL)
    {
        while ((len < maxLen) && (s[len] != '\0'))
        {
            len++;
        }
    }
    return len;
}

status_t safe_strcpy(char *dst, size_t dstSize, const char *src)
{
    status_t result = STATUS_OK;

    if ((dst == NULL) || (src == NULL) || (dstSize == 0U))
    {
        result = STATUS_ERR_INVALID_ARG;
    }
    else
    {
        size_t i = 0U;
        while ((i < (dstSize - 1U)) && (src[i] != '\0'))
        {
            dst[i] = src[i];
            i++;
        }
        dst[i] = '\0';
    }
    return result;
}

int safe_strcasecmp(const char *a, const char *b)
{
    int result = 0;

    if ((a == NULL) || (b == NULL))
    {
        result = (a == b) ? 0 : -1;
    }
    else
    {
        size_t i = 0U;
        for (;;)
        {
            unsigned char ca = (unsigned char)tolower((unsigned char)a[i]);
            unsigned char cb = (unsigned char)tolower((unsigned char)b[i]);
            if (ca != cb)
            {
                result = ((int)ca - (int)cb);
                break;
            }
            if (ca == (unsigned char)'\0')
            {
                result = 0;
                break;
            }
            i++;
        }
    }
    return result;
}

const char *status_to_string(status_t s)
{
    const char *str;
    switch (s)
    {
        case STATUS_OK:               str = "OK"; break;
        case STATUS_ERR_NOT_FOUND:    str = "NOT_FOUND"; break;
        case STATUS_ERR_INVALID_ARG:  str = "INVALID_ARG"; break;
        case STATUS_ERR_IO:           str = "IO_ERROR"; break;
        case STATUS_ERR_FULL:         str = "FULL"; break;
        case STATUS_ERR_DUPLICATE:    str = "DUPLICATE"; break;
        case STATUS_ERR_MEMORY:       str = "MEMORY_ERROR"; break;
        case STATUS_ERR_AUTH:         str = "AUTH_FAILED"; break;
        case STATUS_ERR_PERMISSION:   str = "PERMISSION_DENIED"; break;
        case STATUS_ERR_LOCK:         str = "LOCK_ERROR"; break;
        default:                      str = "UNKNOWN"; break;
    }
    return str;
}

const char *location_status_to_string(location_status_t l)
{
    const char *str;
    switch (l)
    {
        case LOC_NONE:       str = "NOT PRESENT IN EITHER DB"; break;
        case LOC_MAIN_ONLY:  str = "MAIN DB ONLY"; break;
        case LOC_CACHE_ONLY: str = "CACHE DB ONLY"; break;
        case LOC_BOTH:       str = "BOTH MAIN AND CACHE DB"; break;
        default:             str = "UNKNOWN"; break;
    }
    return str;
}
