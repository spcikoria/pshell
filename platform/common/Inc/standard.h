/**
 * @file    standard.h
 * @brief   Common string helpers.
 */
#pragma once

#include <stddef.h>

/**
 * @brief Length of a C string
 * @param[in] pStr Null-terminated string
 * @return Number of characters without the terminator
 */
size_t STD_Strlen(const char *pStr);

/**
 * @brief Copy a C string, the destination is always null-terminated
 * @param[out] pDest Destination buffer
 * @param[in] pSrc Null-terminated source string
 * @param[in] DestSize Size of the destination buffer, a longer string is truncated
 */
void STD_Strncpy(char *pDest, const char *pSrc, size_t DestSize);

/**
 * @brief Compare the beginning of two C strings
 * @param[in] pStr1 First string
 * @param[in] pStr2 Second string
 * @param[in] Length Maximum number of characters to compare
 * @return 0 if they are equal, otherwise the sign shows the order
 */
int STD_Strncmp(const char *pStr1, const char *pStr2, size_t Length);

/**
 * @brief Copy bytes, the two areas must not overlap
 * @param[out] pDest Destination buffer
 * @param[in] pSrc Source buffer
 * @param[in] Length Number of bytes to copy
 */
void STD_Memcpy(void *pDest, const void *pSrc, size_t Length);
