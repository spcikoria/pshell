/**
 * @file    fs_bin.c
 * @brief   /usr/bin: a dynamic folder that lists the commands and apps of the shell.
 *
 * The shell keeps its command table, this folder is only a view of it, nothing is stored twice.
 * The entries are executable but have no content, so they can be listed but not opened.
 */
#include "pshell.h"
#include "fs_tree.h"

/**
 * @brief Entry generator of /usr/bin
 * @param[in] pCtx Unused
 * @param[in] Index Index of the entry
 * @param[out] pInfo Description of the entry
 * @return true if a command exists at the index
 */
static bool FSBIN_GetEntry(const void *pCtx, uint32_t Index, VFS_NodeInfo_t *pInfo)
{
    const PSH_CommandEntry_t *pCommand = NULL;

    (void)pCtx;

    if (Index <= UINT8_MAX)
    {
        pCommand = PSH_GetCommand((uint8_t)Index);
    }

    if (pCommand != NULL)
    {
        pInfo->pName = pCommand->pCommand;
        pInfo->Type = VFS_TYPE_FILE;
        pInfo->Mode = VFS_MODE_X;
        pInfo->Size = 0U;
        pInfo->pFileOps = NULL;
        pInfo->pDirOps = NULL;
        pInfo->pMountOps = NULL;
        pInfo->pCtx = pCommand;
    }
    return pCommand != NULL;
}

static const VFS_DirOps_t FSBIN_DirOps = {
    .GetEntry = FSBIN_GetEntry,
};

static VFS_DYNAMIC_DIR(FSBIN_UsrBin, FSTREE_Usr, "bin", &FSBIN_DirOps, NULL);
