/**
 * @file    vfs_fat.h
 * @brief   FatFs volume on the SD card as a file system of the VFS, mounted with VFS_MOUNT():
 *     VFS_MOUNT(MYTREE_MntSdcard, MYTREE_Mnt, "sdcard", &VFAT_MountOps, NULL);
 *
 * The card can be inserted, removed and changed any time: a call without a card returns -ENODEV,
 * the next call after inserting a card mounts it again. Files open on a removed card return -EBADF.
 * Every FatFs call runs under one mutex, from any task.
 */
#pragma once

#include "vfs_node.h"

/** Number of files open on the card at the same time */
#ifndef VFAT_MAX_FILES
#define VFAT_MAX_FILES (4U)
#endif

/** Number of folders listed at the same time */
#ifndef VFAT_MAX_DIRS
#define VFAT_MAX_DIRS (2U)
#endif

/** Time to wait for the card when another task uses it, then -EBUSY */
#define VFAT_LOCK_TIMEOUT_MS (2000U)

/** File system operations, the context is not used */
extern const VFS_MountOps_t VFAT_MountOps;

/**
 * @brief Create the lock and register the volume, the card is accessed only by the first file operation.
 *        Call it before the scheduler starts.
 */
void VFAT_Init(void);
