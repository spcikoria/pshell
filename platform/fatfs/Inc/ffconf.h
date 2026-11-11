/**
 * @file    ffconf.h
 * @brief   FatFs configuration: FAT12/16/32 on one SD card, long names in UTF-8, small RAM use.
 *
 * Written for FatFs R0.16 (elm-chan.org/fsw/ff), the option names of R0.15 are set too.
 * FFCONF_DEF follows the revision of ff.h, so a newer ff.h compiles, check its new options then.
 * Locking is done by vfs_fat.c around every call, so FF_FS_REENTRANT is off and ffsystem.c is not needed.
 */
#pragma once

#define FFCONF_DEF FF_DEFINED

/* Function configurations */
#define FF_FS_READONLY  0
#define FF_FS_MINIMIZE  0
#define FF_USE_FIND     0
#define FF_USE_MKFS     0
#define FF_USE_FASTSEEK 0
#define FF_USE_EXPAND   0
#define FF_USE_CHMOD    0
#define FF_USE_LABEL    0
#define FF_USE_FORWARD  0
#define FF_USE_STRFUNC  0
#define FF_PRINT_LLI    0
#define FF_PRINT_FLOAT  0
#define FF_STRF_ENCODE  3

/* Namespace and locale: long names with a static buffer (the calls are serialized), UTF-8 API */
#define FF_CODE_PAGE    437
#define FF_USE_LFN      1
#define FF_MAX_LFN      64
#define FF_LFN_UNICODE  2
#define FF_LFN_BUF      64
#define FF_SFN_BUF      12
#define FF_RPATH        0
#define FF_FS_RPATH     0
#define FF_PATH_DEPTH   10

/* Volume: one drive, 512 byte sectors */
#define FF_VOLUMES         1
#define FF_STR_VOLUME_ID   0
#define FF_VOLUME_STRS     "SD"
#define FF_MULTI_PARTITION 0
#define FF_MIN_SS          512
#define FF_MAX_SS          512
#define FF_LBA64           0
#define FF_MIN_GPT         0x10000000
#define FF_USE_TRIM        0

/* System: no sector buffer per file, no exFAT (SDXC cards need FAT32), no RTC */
#define FF_FS_TINY      1
#define FF_FS_EXFAT     0
#define FF_FS_NORTC     1
#define FF_NORTC_MON    1
#define FF_NORTC_MDAY   1
#define FF_NORTC_YEAR   2026
#define FF_FS_CRTIME    0
#define FF_FS_NOFSINFO  0
#define FF_FS_LOCK      0
#define FF_FS_REENTRANT 0
#define FF_FS_TIMEOUT   1000
