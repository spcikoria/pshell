/**
 * @file    fs_tree.h
 * @brief   Linux like folder structure of the virtual file system.
 *
 * Other modules add their nodes under these folders, e.g.
 *     VFS_DEVICE(MYDRV_Node, FSTREE_Dev, "mydev", VFS_MODE_R | VFS_MODE_W, &MYDRV_Ops, NULL);
 */
#pragma once

#include "vfs_node.h"

VFS_NODE_EXTERN(FSTREE_Dev);
VFS_NODE_EXTERN(FSTREE_Etc);
VFS_NODE_EXTERN(FSTREE_Mnt);
VFS_NODE_EXTERN(FSTREE_Proc);
VFS_NODE_EXTERN(FSTREE_Usr);

/**
 * @brief Get the host name, the content of /etc/hostname without the line end
 * @return The host name, it changes when /etc/hostname is written
 */
const char *FSTREE_GetHostname(void);
