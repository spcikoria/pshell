/**
 * @file    diskio.c
 * @brief   FatFs media access on the SD card, drive 0 only.
 *
 * Every error drops the card to "not initialized", so FatFs initializes it again on the next call:
 * this way a card that was removed and inserted works again without a card detect pin.
 * The status check asks the card (CMD13), so a removed or a changed card is noticed before the next access.
 */
#include "ff.h"
#include "diskio.h"
#include "sdcard.h"

/** Drive status, called only under the lock of vfs_fat.c */
static DSTATUS DISKIO_Status = STA_NOINIT;

/*****************************************************************************/
DSTATUS disk_status(BYTE pdrv)
{
    DSTATUS Status = STA_NOINIT;

    if (pdrv == 0U)
    {
        if (((DISKIO_Status & STA_NOINIT) == 0U) && !SDCARD_IsAlive())
        {
            DISKIO_Status |= STA_NOINIT;
        }
        Status = DISKIO_Status;
    }
    return Status;
}

/*****************************************************************************/
DSTATUS disk_initialize(BYTE pdrv)
{
    DSTATUS Status = STA_NOINIT;

    if (pdrv == 0U)
    {
        DISKIO_Status = (SDCARD_Init() == 0) ? 0U : STA_NOINIT;
        Status = DISKIO_Status;
    }
    return Status;
}

/**
 * @brief Check the drive and the parameters of a transfer
 * @param[in] Drive Physical drive number
 * @param[in] Count Number of sectors
 * @return RES_OK if the transfer can start
 */
static DRESULT DISKIO_Check(BYTE Drive, UINT Count)
{
    DRESULT Result = RES_OK;

    if ((Drive != 0U) || (Count == 0U))
    {
        Result = RES_PARERR;
    }
    else if ((DISKIO_Status & STA_NOINIT) != 0U)
    {
        Result = RES_NOTRDY;
    }
    else
    {
        /* Ready */
    }
    return Result;
}

/*****************************************************************************/
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    DRESULT Result = DISKIO_Check(pdrv, count);

    if (Result == RES_OK)
    {
        if (SDCARD_Read(buff, (uint32_t)sector, count) != 0)
        {
            DISKIO_Status |= STA_NOINIT;
            Result = RES_ERROR;
        }
    }
    return Result;
}

/*****************************************************************************/
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    DRESULT Result = DISKIO_Check(pdrv, count);

    if (Result == RES_OK)
    {
        if (SDCARD_Write(buff, (uint32_t)sector, count) != 0)
        {
            DISKIO_Status |= STA_NOINIT;
            Result = RES_ERROR;
        }
    }
    return Result;
}

/*****************************************************************************/
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    DRESULT Result = DISKIO_Check(pdrv, 1U);

    if (Result != RES_OK)
    {
        /* Not ready */
    }
    else if (cmd == CTRL_SYNC)
    {
        if (SDCARD_Sync() != 0)
        {
            DISKIO_Status |= STA_NOINIT;
            Result = RES_ERROR;
        }
    }
    else if (cmd == GET_BLOCK_SIZE)
    {
        /* Erase block size unknown */
        *(DWORD *)buff = 1U;
    }
    else
    {
        /* GET_SECTOR_COUNT is needed only by f_mkfs, which is off */
        Result = RES_PARERR;
    }
    return Result;
}
