#include "standard.h"
#include <string.h>

/*****************************************************************************/
size_t STD_Strlen(const char *pStr)
{
    return strlen(pStr);
}

/*****************************************************************************/
void STD_Strncpy(char *pDest, const char *pSrc, size_t DestSize)
{
    size_t Length;

    if (DestSize != 0U)
    {
        Length = strnlen(pSrc, DestSize - 1U);
        (void)memcpy(pDest, pSrc, Length);
        pDest[Length] = '\0';
    }
}

/*****************************************************************************/
int STD_Strncmp(const char *pStr1, const char *pStr2, size_t Length)
{
    return strncmp(pStr1, pStr2, Length);
}

/*****************************************************************************/
void STD_Memcpy(void *pDest, const void *pSrc, size_t Length)
{
    (void)memcpy(pDest, pSrc, Length);
}
