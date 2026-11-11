#include "vfs_fat.h"
#include "FreeRTOS.h"
#include "ff.h"
#include "semphr.h"
#include <errno.h>
#include <fcntl.h>

#ifndef O_ACCMODE
#define O_ACCMODE (O_RDONLY | O_WRONLY | O_RDWR)
#endif

/** An open file */
typedef struct
{
    FIL Fil;
    bool Used;
} VFAT_File_t;

/** A listed folder, the name of the last entry stays in Info */
typedef struct
{
    DIR Dir;
    FILINFO Info;
    bool Used;
} VFAT_Dir_t;

static FATFS VFAT_Fs;
static VFAT_File_t VFAT_Files[VFAT_MAX_FILES];
static VFAT_Dir_t VFAT_Dirs[VFAT_MAX_DIRS];

/** Serializes every FatFs call and the slot tables */
static StaticSemaphore_t VFAT_MutexBuffer;
static SemaphoreHandle_t VFAT_Mutex;

/**
 * @brief Take the lock of the volume
 * @return true if taken, false on timeout
 */
static bool VFAT_Take(void)
{
    return xSemaphoreTake(VFAT_Mutex, pdMS_TO_TICKS(VFAT_LOCK_TIMEOUT_MS)) == pdTRUE;
}

/**
 * @brief Give back the lock of the volume
 */
static void VFAT_Give(void)
{
    (void)xSemaphoreGive(VFAT_Mutex);
}

/**
 * @brief Convert a FatFs result to an errno value
 * @param[in] Status FatFs result
 * @return 0 or a negative errno value
 */
static int32_t VFAT_Error(FRESULT Status)
{
    int32_t Result;

    switch (Status)
    {
    case FR_OK:
        Result = 0;
        break;
    case FR_NO_FILE:
    case FR_NO_PATH:
        Result = -ENOENT;
        break;
    case FR_INVALID_NAME:
    case FR_INVALID_PARAMETER:
        Result = -EINVAL;
        break;
    case FR_DENIED:
        Result = -EACCES;
        break;
    case FR_EXIST:
        Result = -EEXIST;
        break;
    case FR_INVALID_OBJECT:
        Result = -EBADF;
        break;
    case FR_WRITE_PROTECTED:
        Result = -EROFS;
        break;
    case FR_NOT_READY:
    case FR_INVALID_DRIVE:
    case FR_NOT_ENABLED:
        Result = -ENODEV;
        break;
    case FR_TIMEOUT:
    case FR_LOCKED:
        Result = -EBUSY;
        break;
    case FR_NOT_ENOUGH_CORE:
        Result = -ENOMEM;
        break;
    case FR_TOO_MANY_OPEN_FILES:
        Result = -EMFILE;
        break;
    default:
        /* Disk error, internal error, no FAT volume (e.g. exFAT) */
        Result = -EIO;
        break;
    }
    return Result;
}

/**
 * @brief Fill a node description from a FatFs file information, the name is not set
 * @param[in] pFileInfo FatFs file information
 * @param[out] pInfo Node description
 */
static void VFAT_FillInfo(const FILINFO *pFileInfo, VFS_NodeInfo_t *pInfo)
{
    bool IsDir = ((pFileInfo->fattrib & AM_DIR) != 0U);

    pInfo->Type = IsDir ? VFS_TYPE_DIR : VFS_TYPE_FILE;
    pInfo->Mode = VFS_MODE_R;
    if ((pFileInfo->fattrib & AM_RDO) == 0U)
    {
        pInfo->Mode |= VFS_MODE_W;
    }
    if (IsDir)
    {
        pInfo->Mode |= VFS_MODE_X;
    }
    pInfo->Size = (uint32_t)pFileInfo->fsize;
    pInfo->pFileOps = NULL;
    pInfo->pDirOps = NULL;
    pInfo->pMountOps = NULL;
    pInfo->pCtx = NULL;
}

/**
 * @brief Convert open flags to a FatFs open mode
 * @param[in] Flags O_x flags
 * @return FA_x mode
 */
static BYTE VFAT_OpenMode(int Flags)
{
    BYTE Mode;

    switch (Flags & O_ACCMODE)
    {
    case O_WRONLY:
        Mode = FA_WRITE;
        break;
    case O_RDWR:
        Mode = FA_READ | FA_WRITE;
        break;
    default:
        Mode = FA_READ;
        break;
    }

    if ((Flags & O_CREAT) != 0)
    {
        if ((Flags & O_EXCL) != 0)
        {
            Mode |= FA_CREATE_NEW;
        }
        else if ((Flags & O_TRUNC) != 0)
        {
            Mode |= FA_CREATE_ALWAYS;
        }
        else
        {
            Mode |= FA_OPEN_ALWAYS;
        }
    }
    return Mode;
}

/**
 * @brief Truncate or seek to the end after opening, as the flags ask
 * @param[in,out] pFil The open file
 * @param[in] Flags O_x flags
 * @return FatFs result
 */
static FRESULT VFAT_PrepareFile(FIL *pFil, int Flags)
{
    FRESULT Status = FR_OK;

    /* O_CREAT | O_TRUNC is done by FA_CREATE_ALWAYS, an existing file is truncated here */
    if (((Flags & O_TRUNC) != 0) && ((Flags & O_ACCMODE) != O_RDONLY))
    {
        Status = f_truncate(pFil);
    }
    if ((Status == FR_OK) && ((Flags & O_APPEND) != 0))
    {
        Status = f_lseek(pFil, f_size(pFil));
    }
    return Status;
}

/**
 * @brief Mount operation: describe a file or a folder
 * @param[in] pCtx Unused
 * @param[in] pPath Path inside the volume
 * @param[out] pInfo Node description, the name is set by the VFS
 * @return 0 or a negative errno value
 */
static int32_t VFAT_Stat(const void *pCtx, const char *pPath, VFS_NodeInfo_t *pInfo)
{
    FILINFO FileInfo;
    int32_t Result = -EBUSY;

    (void)pCtx;

    if (VFAT_Take())
    {
        Result = VFAT_Error(f_stat(pPath, &FileInfo));
        VFAT_Give();
    }
    if (Result == 0)
    {
        VFAT_FillInfo(&FileInfo, pInfo);
    }
    return Result;
}

/**
 * @brief Mount operation: open or create a file
 * @param[in] pCtx Unused
 * @param[in] pPath Path inside the volume
 * @param[in] Flags O_x flags
 * @param[out] ppFile File object
 * @return 0 or a negative errno value
 */
static int32_t VFAT_Open(const void *pCtx, const char *pPath, int Flags, void **ppFile)
{
    VFAT_File_t *pFile = NULL;
    FRESULT Status;
    uint32_t Index;
    int32_t Result = -EBUSY;

    (void)pCtx;

    if (VFAT_Take())
    {
        for (Index = 0U; (Index < VFAT_MAX_FILES) && (pFile == NULL); Index++)
        {
            if (!VFAT_Files[Index].Used)
            {
                pFile = &VFAT_Files[Index];
            }
        }

        if (pFile == NULL)
        {
            Result = -EMFILE;
        }
        else
        {
            Status = f_open(&pFile->Fil, pPath, VFAT_OpenMode(Flags));
            if (Status == FR_OK)
            {
                Status = VFAT_PrepareFile(&pFile->Fil, Flags);
                if (Status == FR_OK)
                {
                    pFile->Used = true;
                    *ppFile = pFile;
                }
                else
                {
                    (void)f_close(&pFile->Fil);
                }
            }
            Result = VFAT_Error(Status);
        }
        VFAT_Give();
    }
    return Result;
}

/**
 * @brief Mount operation: close a file, its data is written to the card
 * @param[in] pFile File object
 * @return 0 or a negative errno value, the file object is freed anyway
 */
static int32_t VFAT_Close(void *pFile)
{
    VFAT_File_t *pVfatFile = (VFAT_File_t *)pFile;
    int32_t Result = -EBUSY;

    if (VFAT_Take())
    {
        Result = VFAT_Error(f_close(&pVfatFile->Fil));
        VFAT_Give();
    }
    pVfatFile->Used = false;
    return Result;
}

/**
 * @brief Mount operation: read from a file
 * @param[in] pFile File object
 * @param[out] pBuffer Buffer of the data
 * @param[in] Length Size of the buffer
 * @return Number of bytes read, 0 at the end, or a negative errno value
 */
static int32_t VFAT_Read(void *pFile, void *pBuffer, size_t Length)
{
    VFAT_File_t *pVfatFile = (VFAT_File_t *)pFile;
    UINT Count = 0U;
    int32_t Result = -EBUSY;

    if (VFAT_Take())
    {
        Result = VFAT_Error(f_read(&pVfatFile->Fil, pBuffer, (UINT)Length, &Count));
        VFAT_Give();
    }
    if (Result == 0)
    {
        Result = (int32_t)Count;
    }
    return Result;
}

/**
 * @brief Mount operation: write to a file
 * @param[in] pFile File object
 * @param[in] pData Data to write
 * @param[in] Length Length of the data
 * @return Number of bytes written, or a negative errno value, -ENOSPC on a full card
 */
static int32_t VFAT_Write(void *pFile, const void *pData, size_t Length)
{
    VFAT_File_t *pVfatFile = (VFAT_File_t *)pFile;
    UINT Count = 0U;
    int32_t Result = -EBUSY;

    if (VFAT_Take())
    {
        Result = VFAT_Error(f_write(&pVfatFile->Fil, pData, (UINT)Length, &Count));
        VFAT_Give();
    }
    if ((Result == 0) && (Count == 0U) && (Length != 0U))
    {
        Result = -ENOSPC;
    }
    else if (Result == 0)
    {
        Result = (int32_t)Count;
    }
    else
    {
        /* The error */
    }
    return Result;
}

/**
 * @brief Mount operation: open a folder for listing
 * @param[in] pCtx Unused
 * @param[in] pPath Path inside the volume, "" for the root
 * @param[out] ppDir Folder object
 * @return 0 or a negative errno value
 */
static int32_t VFAT_OpenDir(const void *pCtx, const char *pPath, void **ppDir)
{
    VFAT_Dir_t *pDir = NULL;
    uint32_t Index;
    int32_t Result = -EBUSY;

    (void)pCtx;

    if (VFAT_Take())
    {
        for (Index = 0U; (Index < VFAT_MAX_DIRS) && (pDir == NULL); Index++)
        {
            if (!VFAT_Dirs[Index].Used)
            {
                pDir = &VFAT_Dirs[Index];
            }
        }

        if (pDir == NULL)
        {
            Result = -EMFILE;
        }
        else
        {
            Result = VFAT_Error(f_opendir(&pDir->Dir, pPath));
            if (Result == 0)
            {
                pDir->Used = true;
                *ppDir = pDir;
            }
        }
        VFAT_Give();
    }
    return Result;
}

/**
 * @brief Mount operation: next entry of a folder
 * @param[in] pDir Folder object
 * @param[out] pInfo Entry description, its name is valid until the next call
 * @return 1 for an entry, 0 at the end, or a negative errno value
 */
static int32_t VFAT_ReadDir(void *pDir, VFS_NodeInfo_t *pInfo)
{
    VFAT_Dir_t *pVfatDir = (VFAT_Dir_t *)pDir;
    int32_t Result = -EBUSY;

    if (VFAT_Take())
    {
        Result = VFAT_Error(f_readdir(&pVfatDir->Dir, &pVfatDir->Info));
        VFAT_Give();
    }

    /* An empty name marks the end */
    if ((Result == 0) && (pVfatDir->Info.fname[0] != '\0'))
    {
        VFAT_FillInfo(&pVfatDir->Info, pInfo);
        pInfo->pName = pVfatDir->Info.fname;
        Result = 1;
    }
    return Result;
}

/**
 * @brief Mount operation: finish listing a folder
 * @param[in] pDir Folder object
 */
static void VFAT_CloseDir(void *pDir)
{
    VFAT_Dir_t *pVfatDir = (VFAT_Dir_t *)pDir;

    if (VFAT_Take())
    {
        (void)f_closedir(&pVfatDir->Dir);
        VFAT_Give();
    }
    pVfatDir->Used = false;
}

/**
 * @brief Mount operation: create a folder
 * @param[in] pCtx Unused
 * @param[in] pPath Path inside the volume
 * @return 0 or a negative errno value
 */
static int32_t VFAT_Mkdir(const void *pCtx, const char *pPath)
{
    int32_t Result = -EBUSY;

    (void)pCtx;

    if (VFAT_Take())
    {
        Result = VFAT_Error(f_mkdir(pPath));
        VFAT_Give();
    }
    return Result;
}

/**
 * @brief Mount operation: remove a file or an empty folder
 * @param[in] pCtx Unused
 * @param[in] pPath Path inside the volume
 * @return 0, -ENOTEMPTY or a negative errno value
 */
static int32_t VFAT_Remove(const void *pCtx, const char *pPath)
{
    FILINFO FileInfo;
    FRESULT Status;
    int32_t Result = -EBUSY;

    (void)pCtx;

    if (VFAT_Take())
    {
        Status = f_unlink(pPath);
        Result = VFAT_Error(Status);

        /* Denied for a read-only file and for a folder that is not empty */
        if ((Status == FR_DENIED) && (f_stat(pPath, &FileInfo) == FR_OK) && ((FileInfo.fattrib & AM_DIR) != 0U))
        {
            Result = -ENOTEMPTY;
        }
        VFAT_Give();
    }
    return Result;
}

/*****************************************************************************/
const VFS_MountOps_t VFAT_MountOps = {
    .Stat = VFAT_Stat,
    .Open = VFAT_Open,
    .Close = VFAT_Close,
    .Read = VFAT_Read,
    .Write = VFAT_Write,
    .OpenDir = VFAT_OpenDir,
    .ReadDir = VFAT_ReadDir,
    .CloseDir = VFAT_CloseDir,
    .Mkdir = VFAT_Mkdir,
    .Remove = VFAT_Remove,
};

/*****************************************************************************/
void VFAT_Init(void)
{
    VFAT_Mutex = xSemaphoreCreateMutexStatic(&VFAT_MutexBuffer);
    configASSERT(VFAT_Mutex != NULL);

    /* Delayed mount: no card access here, the first file operation mounts the volume */
    (void)f_mount(&VFAT_Fs, "", 0U);
}
