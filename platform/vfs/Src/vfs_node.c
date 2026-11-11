#include "vfs_node.h"
#include "standard.h"
#include <errno.h>

/** Bounds of the .vfs_nodes section, from the linker script */
extern const VFS_Node_t __vfs_nodes_start__[];
extern const VFS_Node_t __vfs_nodes_end__[];

/*****************************************************************************/
const VFS_Node_t VFS_RootNode = {
    .pParent = NULL,
    .Info = {
        .pName = "",
        .Type = VFS_TYPE_DIR,
        .Mode = VFS_MODE_R | VFS_MODE_X,
        .pFileOps = NULL,
        .pDirOps = NULL,
        .pCtx = NULL,
    },
};

/**
 * @brief Length of the first component of a path
 * @param[in] pPath Path, starting at the component
 * @return Number of characters until the next '/' or the end
 */
static size_t VFS_ComponentLength(const char *pPath)
{
    size_t Length = 0U;

    while ((pPath[Length] != '\0') && (pPath[Length] != '/'))
    {
        Length++;
    }
    return Length;
}

/**
 * @brief Compare a node name with a path component
 * @param[in] pName Null-terminated node name
 * @param[in] pComponent Path component, not terminated
 * @param[in] Length Length of the component
 * @return true if they are equal
 */
static bool VFS_NameEquals(const char *pName, const char *pComponent, size_t Length)
{
    return (STD_Strncmp(pName, pComponent, Length) == 0) && (pName[Length] == '\0');
}

/**
 * @brief Append the components of a path to a canonical path: "." is skipped, ".." removes the last component
 * @param[in,out] pOut Canonical absolute path
 * @param[in,out] pLength Length of the canonical path
 * @param[in] Size Size of the buffer of the canonical path
 * @param[in] pPath Components to append
 * @return 0 or -ENAMETOOLONG
 */
static int32_t VFS_AppendComponents(char *pOut, size_t *pLength, size_t Size, const char *pPath)
{
    size_t Length = *pLength;
    size_t ComponentLength;
    size_t Separator;
    int32_t Result = 0;

    while ((*pPath != '\0') && (Result == 0))
    {
        if (*pPath == '/')
        {
            pPath++;
        }
        else
        {
            ComponentLength = VFS_ComponentLength(pPath);

            if ((ComponentLength == 1U) && (pPath[0] == '.'))
            {
                /* The same folder */
            }
            else if ((ComponentLength == 2U) && (pPath[0] == '.') && (pPath[1] == '.'))
            {
                /* Back to the parent, the root is its own parent */
                while ((Length > 1U) && (pOut[Length - 1U] != '/'))
                {
                    Length--;
                }
                if (Length > 1U)
                {
                    Length--;
                }
            }
            else
            {
                /* The root already ends with the separator */
                Separator = (Length > 1U) ? 1U : 0U;

                if ((Length + Separator + ComponentLength) >= Size)
                {
                    Result = -ENAMETOOLONG;
                }
                else
                {
                    if (Separator != 0U)
                    {
                        pOut[Length] = '/';
                        Length++;
                    }
                    STD_Memcpy(&pOut[Length], pPath, ComponentLength);
                    Length += ComponentLength;
                }
            }
            pPath += ComponentLength;
        }
    }

    pOut[Length] = '\0';
    *pLength = Length;
    return Result;
}

/**
 * @brief Find a child of a folder: the static nodes first, then the generated entries
 * @param[in] pDirInfo Description of the folder
 * @param[in] pDirNode Static node of the folder, NULL for an entry of a dynamic folder
 * @param[in] pName Name of the child, not terminated
 * @param[in] Length Length of the name
 * @param[out] pInfo Description of the child
 * @param[out] ppNode Static node of the child, NULL for a generated entry
 * @return true if found
 */
static bool VFS_FindChild(const VFS_NodeInfo_t *pDirInfo,
                          const VFS_Node_t *pDirNode,
                          const char *pName,
                          size_t Length,
                          VFS_NodeInfo_t *pInfo,
                          const VFS_Node_t **ppNode)
{
    const VFS_Node_t *pNode;
    uint32_t Index = 0U;
    bool Found = false;

    /* An entry of a dynamic folder has no static children */
    if (pDirNode != NULL)
    {
        for (pNode = __vfs_nodes_start__; (pNode < __vfs_nodes_end__) && !Found; pNode++)
        {
            if ((pNode->pParent == pDirNode) && VFS_NameEquals(pNode->Info.pName, pName, Length))
            {
                *pInfo = pNode->Info;
                *ppNode = pNode;
                Found = true;
            }
        }
    }

    if (!Found && (pDirInfo->pDirOps != NULL))
    {
        while (!Found && pDirInfo->pDirOps->GetEntry(pDirInfo->pCtx, Index, pInfo))
        {
            if (VFS_NameEquals(pInfo->pName, pName, Length))
            {
                *ppNode = NULL;
                Found = true;
            }
            Index++;
        }
    }
    return Found;
}

/**
 * @brief Last component of a canonical path
 * @param[in] pPath Canonical absolute path
 * @return The name after the last '/'
 */
static const char *VFS_LastComponent(const char *pPath)
{
    const char *pName = pPath;

    while (*pPath != '\0')
    {
        if (*pPath == '/')
        {
            pName = pPath + 1;
        }
        pPath++;
    }
    return pName;
}

/*****************************************************************************/
int32_t VFS_JoinPath(const char *pBase, const char *pPath, char *pOut, size_t Size)
{
    size_t Length = 1U;
    int32_t Result = 0;

    if ((pPath == NULL) || (pOut == NULL) || (Size < 2U))
    {
        Result = -EINVAL;
    }
    else
    {
        pOut[0] = '/';
        pOut[1] = '\0';

        if ((*pPath != '/') && (pBase != NULL))
        {
            Result = VFS_AppendComponents(pOut, &Length, Size, pBase);
        }
        if (Result == 0)
        {
            Result = VFS_AppendComponents(pOut, &Length, Size, pPath);
        }
    }
    return Result;
}

/*****************************************************************************/
int32_t VFS_Lookup(const char *pPath, VFS_Entry_t *pEntry)
{
    const char *pComponent = &pEntry->Path[1];
    const VFS_Node_t *pNode = NULL;
    VFS_NodeInfo_t Info;
    size_t Length;
    int32_t Result;

    Result = VFS_JoinPath(NULL, pPath, pEntry->Path, sizeof(pEntry->Path));

    pEntry->Info = VFS_RootNode.Info;
    pEntry->pNode = &VFS_RootNode;
    pEntry->pMountOps = NULL;
    pEntry->pMountCtx = NULL;
    pEntry->pMountPath = NULL;

    /* The canonical path has no empty, "." and ".." components, the walk stops at a mount point */
    while ((Result == 0) && (*pComponent != '\0') && (pEntry->pMountOps == NULL))
    {
        Length = VFS_ComponentLength(pComponent);

        if (pEntry->Info.Type != VFS_TYPE_DIR)
        {
            Result = -ENOTDIR;
        }
        else if (!VFS_FindChild(&pEntry->Info, pEntry->pNode, pComponent, Length, &Info, &pNode))
        {
            Result = -ENOENT;
        }
        else
        {
            pEntry->Info = Info;
            pEntry->pNode = pNode;
            pComponent += Length;
            if (*pComponent == '/')
            {
                pComponent++;
            }
            if (Info.pMountOps != NULL)
            {
                pEntry->pMountOps = Info.pMountOps;
                pEntry->pMountCtx = Info.pCtx;
                pEntry->pMountPath = pComponent;
            }
        }
    }

    /* Below the mount point only the file system knows the nodes */
    if ((Result == 0) && (pEntry->pMountOps != NULL) && (*pEntry->pMountPath != '\0'))
    {
        Result = pEntry->pMountOps->Stat(pEntry->pMountCtx, pEntry->pMountPath, &pEntry->Info);
        pEntry->Info.pName = VFS_LastComponent(pEntry->Path);
        pEntry->pNode = NULL;
    }
    return Result;
}

/*****************************************************************************/
int32_t VFS_Stat(const char *pPath, VFS_NodeInfo_t *pInfo)
{
    VFS_Entry_t Entry;
    int32_t Result;

    Result = VFS_Lookup(pPath, &Entry);
    if (Result == 0)
    {
        *pInfo = Entry.Info;

        /* The name of a mounted entry points into the local entry */
        if ((Entry.pMountOps != NULL) && (*Entry.pMountPath != '\0'))
        {
            pInfo->pName = "";
        }
    }
    return Result;
}

/*****************************************************************************/
int32_t VFS_OpenDir(const char *pPath, VFS_Dir_t *pDir)
{
    int32_t Result;

    Result = VFS_Lookup(pPath, &pDir->Dir);
    pDir->pNext = __vfs_nodes_start__;
    pDir->DynamicIndex = 0U;
    pDir->pMountDir = NULL;

    if (Result != 0)
    {
        /* The error of the lookup */
    }
    else if (pDir->Dir.Info.Type != VFS_TYPE_DIR)
    {
        Result = -ENOTDIR;
    }
    else if (pDir->Dir.pMountOps != NULL)
    {
        Result = pDir->Dir.pMountOps->OpenDir(pDir->Dir.pMountCtx, pDir->Dir.pMountPath, &pDir->pMountDir);
    }
    else
    {
        /* Static and generated entries need no opening */
    }
    return Result;
}

/*****************************************************************************/
bool VFS_ReadDir(VFS_Dir_t *pDir, VFS_NodeInfo_t *pInfo)
{
    const VFS_Node_t *pNode;
    bool Found = false;

    if (pDir->pMountDir != NULL)
    {
        Found = (pDir->Dir.pMountOps->ReadDir(pDir->pMountDir, pInfo) == 1);
    }
    else
    {
        if (pDir->Dir.pNode != NULL)
        {
            while (!Found && (pDir->pNext < __vfs_nodes_end__))
            {
                pNode = pDir->pNext;
                pDir->pNext++;

                if (pNode->pParent == pDir->Dir.pNode)
                {
                    *pInfo = pNode->Info;
                    Found = true;
                }
            }
        }

        if (!Found && (pDir->Dir.Info.pDirOps != NULL))
        {
            if (pDir->Dir.Info.pDirOps->GetEntry(pDir->Dir.Info.pCtx, pDir->DynamicIndex, pInfo))
            {
                pDir->DynamicIndex++;
                Found = true;
            }
        }
    }
    return Found;
}

/*****************************************************************************/
void VFS_CloseDir(VFS_Dir_t *pDir)
{
    if (pDir->pMountDir != NULL)
    {
        pDir->Dir.pMountOps->CloseDir(pDir->pMountDir);
        pDir->pMountDir = NULL;
    }
}

/*****************************************************************************/
int32_t VFS_ReadString(const char *pText, uint32_t Offset, void *pBuffer, size_t Length)
{
    size_t Size = STD_Strlen(pText);
    size_t Count = 0U;

    if (Offset < Size)
    {
        Count = Size - Offset;
        if (Count > Length)
        {
            Count = Length;
        }
        STD_Memcpy(pBuffer, &pText[Offset], Count);
    }
    return (int32_t)Count;
}

/**
 * @brief Read handler of a text file
 * @param[in] pCtx The null-terminated text
 * @param[in] Offset Read position
 * @param[out] pBuffer Buffer of the data
 * @param[in] Length Size of the buffer
 * @return Number of bytes read, 0 at the end of the text
 */
static int32_t VFS_TextRead(const void *pCtx, uint32_t Offset, void *pBuffer, size_t Length)
{
    return VFS_ReadString((const char *)pCtx, Offset, pBuffer, Length);
}

/*****************************************************************************/
const VFS_FileOps_t VFS_TextOps = {
    .Read = VFS_TextRead,
    .Write = NULL,
};
