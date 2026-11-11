/**
 * @file    fs_tree.c
 * @brief   Folders of the virtual file system and the files that belong to no driver.
 */
#include "fs_tree.h"
#include "FreeRTOS.h"
#include "standard.h"
#include "task.h"
#include "vfs_fat.h"
#include <errno.h>
#include <stdio.h>

/** Size of the host name with the terminator */
#define FSTREE_HOSTNAME_SIZE (16U)

/** Host name, writable through /etc/hostname, kept in RAM until the next reset */
static char FSTREE_Hostname[FSTREE_HOSTNAME_SIZE] = "pshell";

/* Folders */
VFS_DIR(FSTREE_Dev, VFS_RootNode, "dev");
VFS_DIR(FSTREE_Etc, VFS_RootNode, "etc");
VFS_DIR(FSTREE_Mnt, VFS_RootNode, "mnt");
VFS_DIR(FSTREE_Proc, VFS_RootNode, "proc");
VFS_DIR(FSTREE_Usr, VFS_RootNode, "usr");

/** Mount point of the SD card, the FAT volume serves everything below it */
static VFS_MOUNT(FSTREE_MntSdcard, FSTREE_Mnt, "sdcard", &VFAT_MountOps, NULL);

/** /etc/issue: constant text */
static VFS_TEXT_FILE(FSTREE_EtcIssue, FSTREE_Etc, "issue", "PSHELL on STM32H5\n");

/**
 * @brief Read handler of /dev/null: always at the end of the file
 * @param[in] pCtx Unused
 * @param[in] Offset Unused
 * @param[out] pBuffer Unused
 * @param[in] Length Unused
 * @return 0
 */
static int32_t FSTREE_NullRead(const void *pCtx, uint32_t Offset, void *pBuffer, size_t Length)
{
    (void)pCtx;
    (void)Offset;
    (void)pBuffer;
    (void)Length;

    return 0;
}

/**
 * @brief Write handler of /dev/null: drops the data
 * @param[in] pCtx Unused
 * @param[in] Offset Unused
 * @param[in] pData Unused
 * @param[in] Length Length of the data
 * @return Length, all of the data is accepted
 */
static int32_t FSTREE_NullWrite(const void *pCtx, uint32_t Offset, const void *pData, size_t Length)
{
    (void)pCtx;
    (void)Offset;
    (void)pData;

    return (int32_t)Length;
}

static const VFS_FileOps_t FSTREE_NullOps = {
    .Read = FSTREE_NullRead,
    .Write = FSTREE_NullWrite,
};

static VFS_DEVICE(FSTREE_DevNull, FSTREE_Dev, "null", VFS_MODE_R | VFS_MODE_W, &FSTREE_NullOps, NULL);

/**
 * @brief Read handler of /etc/hostname
 * @param[in] pCtx Unused
 * @param[in] Offset Read position
 * @param[out] pBuffer Buffer of the data
 * @param[in] Length Size of the buffer
 * @return Number of bytes read, 0 at the end
 */
static int32_t FSTREE_HostnameRead(const void *pCtx, uint32_t Offset, void *pBuffer, size_t Length)
{
    char Text[FSTREE_HOSTNAME_SIZE + 1U];

    (void)pCtx;

    (void)snprintf(Text, sizeof(Text), "%s\n", FSTREE_Hostname);
    return VFS_ReadString(Text, Offset, pBuffer, Length);
}

/**
 * @brief Write handler of /etc/hostname, every write sets the whole name
 * @note  Not locked: a reader running at the same time can see a half written name
 * @param[in] pCtx Unused
 * @param[in] Offset Unused, every write is a complete value
 * @param[in] pData New name, the line end is removed
 * @param[in] Length Length of the data
 * @return Length, or -EINVAL for an empty or too long name
 */
static int32_t FSTREE_HostnameWrite(const void *pCtx, uint32_t Offset, const void *pData, size_t Length)
{
    const char *pText = (const char *)pData;
    size_t TextLength = Length;
    int32_t Result = (int32_t)Length;

    (void)pCtx;
    (void)Offset;

    /* echo adds a line end */
    while ((TextLength > 0U) && ((pText[TextLength - 1U] == '\n') || (pText[TextLength - 1U] == '\r')))
    {
        TextLength--;
    }

    if ((TextLength == 0U) || (TextLength >= sizeof(FSTREE_Hostname)))
    {
        Result = -EINVAL;
    }
    else
    {
        STD_Memcpy(FSTREE_Hostname, pText, TextLength);
        FSTREE_Hostname[TextLength] = '\0';
    }
    return Result;
}

static const VFS_FileOps_t FSTREE_HostnameOps = {
    .Read = FSTREE_HostnameRead,
    .Write = FSTREE_HostnameWrite,
};

static VFS_FILE(FSTREE_EtcHostname, FSTREE_Etc, "hostname", VFS_MODE_R | VFS_MODE_W, &FSTREE_HostnameOps, NULL);

/*****************************************************************************/
const char *FSTREE_GetHostname(void)
{
    return FSTREE_Hostname;
}

/**
 * @brief Read handler of /proc/uptime: seconds since the start, with two decimals
 * @param[in] pCtx Unused
 * @param[in] Offset Read position
 * @param[out] pBuffer Buffer of the data
 * @param[in] Length Size of the buffer
 * @return Number of bytes read, 0 at the end
 */
static int32_t FSTREE_UptimeRead(const void *pCtx, uint32_t Offset, void *pBuffer, size_t Length)
{
    char Text[24];
    TickType_t Ticks = xTaskGetTickCount();

    (void)pCtx;

    (void)snprintf(Text,
                   sizeof(Text),
                   "%lu.%02lu\n",
                   (unsigned long)(Ticks / configTICK_RATE_HZ),
                   (unsigned long)(((Ticks % configTICK_RATE_HZ) * 100U) / configTICK_RATE_HZ));
    return VFS_ReadString(Text, Offset, pBuffer, Length);
}

static const VFS_FileOps_t FSTREE_UptimeOps = {
    .Read = FSTREE_UptimeRead,
    .Write = NULL,
};

static VFS_FILE(FSTREE_ProcUptime, FSTREE_Proc, "uptime", VFS_MODE_R, &FSTREE_UptimeOps, NULL);
