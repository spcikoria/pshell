/**
 * @file    vfs.h
 * @brief   File API of the virtual file system: open, read, write and close by file descriptor.
 *
 * The functions return negative errno values on error, like the Linux kernel, so a later newlib
 * _open()/_read()/_write() layer can pass them on. The open flags are the standard O_RDONLY,
 * O_WRONLY and O_RDWR from <fcntl.h>, with O_CREAT, O_EXCL, O_TRUNC and O_APPEND.
 * The static tree is read-only: creating or removing a node there gives -EROFS. Below a mount point
 * the file system does it.
 */
#pragma once

#include "vfs_node.h"
#include <fcntl.h>

/** Number of files open at the same time */
#define VFS_MAX_OPEN_FILES (8U)

/**
 * @brief  Open a file or a device, folders are listed with VFS_OpenDir()
 * @param[in]  pPath  Path, a relative path starts from the root
 * @param[in]  Flags  O_RDONLY, O_WRONLY or O_RDWR, optionally O_CREAT, O_EXCL, O_TRUNC, O_APPEND
 * @return File descriptor (0 or more), or -ENOENT, -EISDIR, -EACCES, -EROFS, -EMFILE, -EINVAL,
 *         or an error of the mounted file system
 */
int32_t VFS_Open(const char *pPath, int Flags);

/**
 * @brief  Close a file, a mounted file system writes its buffers here
 * @param[in]  Fd  File descriptor
 * @return 0, -EBADF, or an error of the mounted file system (the descriptor is closed anyway)
 */
int32_t VFS_Close(int32_t Fd);

/**
 * @brief  Read from a file, the read handler of the file runs in the task of the caller
 * @param[in]  Fd  File descriptor, opened for reading
 * @param[out] pBuffer  Buffer of the data
 * @param[in]  Length  Size of the buffer
 * @return Number of bytes read, 0 at the end of the file, or -EBADF, -EINVAL, or an error of the handler
 */
int32_t VFS_Read(int32_t Fd, void *pBuffer, size_t Length);

/**
 * @brief  Write to a file, the write handler of the file runs in the task of the caller
 * @param[in]  Fd  File descriptor, opened for writing
 * @param[in]  pData  Data to write
 * @param[in]  Length  Length of the data
 * @return Number of bytes written, or -EBADF, -EINVAL, or an error of the handler
 */
int32_t VFS_Write(int32_t Fd, const void *pData, size_t Length);

/**
 * @brief  Create a folder, only below a mount point
 * @param[in]  pPath  Path of the new folder
 * @return 0, -EEXIST, -EROFS, -ENOENT (no parent) or an error of the file system
 */
int32_t VFS_Mkdir(const char *pPath);

/**
 * @brief  Remove a file, only below a mount point
 * @param[in]  pPath  Path of the file
 * @return 0, -EISDIR, -EROFS, -ENOENT or an error of the file system
 */
int32_t VFS_Unlink(const char *pPath);

/**
 * @brief  Remove an empty folder, only below a mount point
 * @param[in]  pPath  Path of the folder
 * @return 0, -ENOTDIR, -ENOTEMPTY, -EBUSY (a mount point), -EROFS, -ENOENT or an error of the file system
 */
int32_t VFS_Rmdir(const char *pPath);

/**
 * @brief  Text of an error code, a short replacement of strerror()
 * @param[in]  Error  Negative errno value
 * @return Message, never NULL
 */
const char *VFS_StrError(int32_t Error);
