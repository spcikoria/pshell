/**
 * @file    error.h
 * @brief   Common error codes.
 */
#pragma once

/** Result of an operation */
typedef enum
{
    ERR_OK = 0,
    ERR_INVALID_PARAMETER,
    ERR_FULL,
} ERR_t;
