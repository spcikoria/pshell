#include "vfs.h"
#include "FreeRTOS.h"
#include "task.h"
#include <errno.h>

#ifndef O_ACCMODE
#define O_ACCMODE (O_RDONLY | O_WRONLY | O_RDWR)
#endif

/** Access of an open file, 0 marks a free slot */
#define VFS_ACCESS_READ  (0x1U)
#define VFS_ACCESS_WRITE (0x2U)

/** An open file: a static file served by handlers, or a file of a mounted file system */
typedef struct
{
    /* Handlers of a static file, NULL for a mounted file */
    const VFS_FileOps_t *pOps;
    /* File system of a mounted file, NULL for a static file */
    const VFS_MountOps_t *pMountOps;
    /* Context of the handlers */
    const void *pCtx;
    /* File object of the file system */
    void *pMountFile;
    uint32_t Offset;
    uint8_t Access;
} VFS_File_t;

static VFS_File_t VFS_Files[VFS_MAX_OPEN_FILES];

/**
 * @brief Check if an entry is inside a mounted file system, below its mount point
 * @param[in] pEntry Result of VFS_Lookup(), also after -ENOENT
 * @return true if the file system serves the entry
 */
static bool VFS_IsInMount(const VFS_Entry_t *pEntry)
{
    return (pEntry->pMountOps != NULL) && (*pEntry->pMountPath != '\0');
}

/**
 * @brief Check the mode and the handlers of a node against the requested access
 * @param[in] pInfo The node
 * @param[in] Access VFS_ACCESS_x bits
 * @return true if every requested access is allowed
 */
static bool VFS_IsAllowed(const VFS_NodeInfo_t *pInfo, uint8_t Access)
{
    bool Allowed = (pInfo->pFileOps != NULL);

    if (Allowed && ((Access & VFS_ACCESS_READ) != 0U))
    {
        Allowed = ((pInfo->Mode & VFS_MODE_R) != 0U) && (pInfo->pFileOps->Read != NULL);
    }
    if (Allowed && ((Access & VFS_ACCESS_WRITE) != 0U))
    {
        Allowed = ((pInfo->Mode & VFS_MODE_W) != 0U) && (pInfo->pFileOps->Write != NULL);
    }
    return Allowed;
}

/**
 * @brief Take a free slot of the file table
 * @param[in] pOps Handlers of a static file, NULL for a mounted file
 * @param[in] pMountOps File system of a mounted file, NULL for a static file
 * @param[in] pCtx Context of the handlers
 * @param[in] Access VFS_ACCESS_x bits
 * @return File descriptor or -EMFILE
 */
static int32_t VFS_AllocFile(const VFS_FileOps_t *pOps, const VFS_MountOps_t *pMountOps, const void *pCtx, uint8_t Access)
{
    int32_t Fd = -EMFILE;
    uint32_t Index;

    /* Any task can open files, the search and the take are one step */
    taskENTER_CRITICAL();
    for (Index = 0U; (Index < VFS_MAX_OPEN_FILES) && (Fd < 0); Index++)
    {
        if (VFS_Files[Index].Access == 0U)
        {
            VFS_Files[Index].pOps = pOps;
            VFS_Files[Index].pMountOps = pMountOps;
            VFS_Files[Index].pCtx = pCtx;
            VFS_Files[Index].pMountFile = NULL;
            VFS_Files[Index].Offset = 0U;
            VFS_Files[Index].Access = Access;
            Fd = (int32_t)Index;
        }
    }
    taskEXIT_CRITICAL();

    return Fd;
}

/**
 * @brief Open a file of a mounted file system, the slot is taken first so a full table does not leave it open
 * @param[in] pEntry Result of VFS_Lookup() inside the file system
 * @param[in] Flags Open flags
 * @param[in] Access VFS_ACCESS_x bits
 * @return File descriptor, -EMFILE or an error of the file system
 */
static int32_t VFS_OpenMounted(const VFS_Entry_t *pEntry, int Flags, uint8_t Access)
{
    void *pMountFile = NULL;
    int32_t Fd;
    int32_t Result;

    Fd = VFS_AllocFile(NULL, pEntry->pMountOps, NULL, Access);
    Result = Fd;

    if (Fd >= 0)
    {
        Result = pEntry->pMountOps->Open(pEntry->pMountCtx, pEntry->pMountPath, Flags, &pMountFile);
        if (Result == 0)
        {
            /* Only the owner of the descriptor uses it */
            VFS_Files[Fd].pMountFile = pMountFile;
            Result = Fd;
        }
        else
        {
            VFS_Files[Fd].Access = 0U;
        }
    }
    return Result;
}

/**
 * @brief Get an open file
 * @param[in] Fd File descriptor
 * @param[in] Access The file has to be open for one of these VFS_ACCESS_x bits
 * @return The file, NULL if the descriptor is not valid for the access
 */
static VFS_File_t *VFS_GetFile(int32_t Fd, uint8_t Access)
{
    VFS_File_t *pFile = NULL;

    if ((Fd >= 0) && (Fd < (int32_t)VFS_MAX_OPEN_FILES) && ((VFS_Files[Fd].Access & Access) != 0U))
    {
        pFile = &VFS_Files[Fd];
    }
    return pFile;
}

/*****************************************************************************/
int32_t VFS_Open(const char *pPath, int Flags)
{
    VFS_Entry_t Entry;
    uint8_t Access;
    int32_t Result;

    switch (Flags & O_ACCMODE)
    {
    case O_RDONLY:
        Access = VFS_ACCESS_READ;
        break;
    case O_WRONLY:
        Access = VFS_ACCESS_WRITE;
        break;
    case O_RDWR:
        Access = VFS_ACCESS_READ | VFS_ACCESS_WRITE;
        break;
    default:
        Access = 0U;
        break;
    }

    Result = VFS_Lookup(pPath, &Entry);

    if (Access == 0U)
    {
        Result = -EINVAL;
    }
    else if ((Result == -ENOENT) && ((Flags & O_CREAT) != 0))
    {
        /* Only a mounted file system can create a file */
        Result = VFS_IsInMount(&Entry) ? VFS_OpenMounted(&Entry, Flags, Access) : -EROFS;
    }
    else if (Result != 0)
    {
        /* The error of the lookup */
    }
    else if (Entry.Info.Type == VFS_TYPE_DIR)
    {
        Result = -EISDIR;
    }
    else if (VFS_IsInMount(&Entry))
    {
        Result = VFS_OpenMounted(&Entry, Flags, Access);
    }
    else if ((Flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL))
    {
        Result = -EEXIST;
    }
    else if (!VFS_IsAllowed(&Entry.Info, Access))
    {
        Result = -EACCES;
    }
    else
    {
        Result = VFS_AllocFile(Entry.Info.pFileOps, NULL, Entry.Info.pCtx, Access);
    }
    return Result;
}

/*****************************************************************************/
int32_t VFS_Close(int32_t Fd)
{
    VFS_File_t *pFile = VFS_GetFile(Fd, VFS_ACCESS_READ | VFS_ACCESS_WRITE);
    int32_t Result = -EBADF;

    if (pFile != NULL)
    {
        Result = 0;
        if (pFile->pMountOps != NULL)
        {
            Result = pFile->pMountOps->Close(pFile->pMountFile);
        }
        pFile->Access = 0U;
    }
    return Result;
}

/*****************************************************************************/
int32_t VFS_Read(int32_t Fd, void *pBuffer, size_t Length)
{
    VFS_File_t *pFile = VFS_GetFile(Fd, VFS_ACCESS_READ);
    int32_t Result;

    if (pFile == NULL)
    {
        Result = -EBADF;
    }
    else if ((pBuffer == NULL) && (Length != 0U))
    {
        Result = -EINVAL;
    }
    else
    {
        if (pFile->pMountOps != NULL)
        {
            Result = pFile->pMountOps->Read(pFile->pMountFile, pBuffer, Length);
        }
        else
        {
            Result = pFile->pOps->Read(pFile->pCtx, pFile->Offset, pBuffer, Length);
        }
        if (Result > 0)
        {
            pFile->Offset += (uint32_t)Result;
        }
    }
    return Result;
}

/*****************************************************************************/
int32_t VFS_Write(int32_t Fd, const void *pData, size_t Length)
{
    VFS_File_t *pFile = VFS_GetFile(Fd, VFS_ACCESS_WRITE);
    int32_t Result;

    if (pFile == NULL)
    {
        Result = -EBADF;
    }
    else if ((pData == NULL) && (Length != 0U))
    {
        Result = -EINVAL;
    }
    else
    {
        if (pFile->pMountOps != NULL)
        {
            Result = pFile->pMountOps->Write(pFile->pMountFile, pData, Length);
        }
        else
        {
            Result = pFile->pOps->Write(pFile->pCtx, pFile->Offset, pData, Length);
        }
        if (Result > 0)
        {
            pFile->Offset += (uint32_t)Result;
        }
    }
    return Result;
}

/*****************************************************************************/
int32_t VFS_Mkdir(const char *pPath)
{
    VFS_Entry_t Entry;
    int32_t Result;

    Result = VFS_Lookup(pPath, &Entry);

    if (Result == 0)
    {
        Result = -EEXIST;
    }
    else if ((Result == -ENOENT) && VFS_IsInMount(&Entry))
    {
        Result = Entry.pMountOps->Mkdir(Entry.pMountCtx, Entry.pMountPath);
    }
    else if (Result == -ENOENT)
    {
        Result = -EROFS;
    }
    else
    {
        /* The error of the lookup */
    }
    return Result;
}

/*****************************************************************************/
int32_t VFS_Unlink(const char *pPath)
{
    VFS_Entry_t Entry;
    int32_t Result;

    Result = VFS_Lookup(pPath, &Entry);

    if (Result != 0)
    {
        /* The error of the lookup */
    }
    else if (Entry.Info.Type == VFS_TYPE_DIR)
    {
        Result = -EISDIR;
    }
    else if (!VFS_IsInMount(&Entry))
    {
        Result = -EROFS;
    }
    else
    {
        Result = Entry.pMountOps->Remove(Entry.pMountCtx, Entry.pMountPath);
    }
    return Result;
}

/*****************************************************************************/
int32_t VFS_Rmdir(const char *pPath)
{
    VFS_Entry_t Entry;
    int32_t Result;

    Result = VFS_Lookup(pPath, &Entry);

    if (Result != 0)
    {
        /* The error of the lookup */
    }
    else if (Entry.Info.Type != VFS_TYPE_DIR)
    {
        Result = -ENOTDIR;
    }
    else if (VFS_IsInMount(&Entry))
    {
        Result = Entry.pMountOps->Remove(Entry.pMountCtx, Entry.pMountPath);
    }
    else if (Entry.pMountOps != NULL)
    {
        /* The mount point itself */
        Result = -EBUSY;
    }
    else
    {
        Result = -EROFS;
    }
    return Result;
}

/*****************************************************************************/
const char *VFS_StrError(int32_t Error)
{
    const char *pText;

    switch (-Error)
    {
    case 0:
        pText = "Success";
        break;
    case ENOENT:
        pText = "No such file or directory";
        break;
    case EBADF:
        pText = "Bad file descriptor";
        break;
    case EACCES:
        pText = "Permission denied";
        break;
    case ENOTDIR:
        pText = "Not a directory";
        break;
    case EISDIR:
        pText = "Is a directory";
        break;
    case EINVAL:
        pText = "Invalid argument";
        break;
    case EMFILE:
        pText = "Too many open files";
        break;
    case EROFS:
        pText = "Read-only file system";
        break;
    case ENAMETOOLONG:
        pText = "File name too long";
        break;
    case EIO:
        pText = "Input/output error";
        break;
    case ENODEV:
        pText = "No such device";
        break;
    case EEXIST:
        pText = "File exists";
        break;
    case ENOTEMPTY:
        pText = "Directory not empty";
        break;
    case ENOSPC:
        pText = "No space left on device";
        break;
    case EBUSY:
        pText = "Device or resource busy";
        break;
    case ENOMEM:
        pText = "Out of memory";
        break;
    default:
        pText = "Unknown error";
        break;
    }
    return pText;
}
