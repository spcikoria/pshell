#include "sdcard.h"
#include "FreeRTOS.h"
#include "bsp_sdspi.h"
#include "task.h"
#include <errno.h>

/** Commands, the application commands are sent after CMD55 */
#define SDCARD_ACMD   (0x80U)
#define SDCARD_CMD0   (0U)
#define SDCARD_CMD8   (8U)
#define SDCARD_CMD13  (13U)
#define SDCARD_CMD16  (16U)
#define SDCARD_CMD17  (17U)
#define SDCARD_CMD24  (24U)
#define SDCARD_CMD55  (55U)
#define SDCARD_CMD58  (58U)
#define SDCARD_ACMD41 (SDCARD_ACMD | 41U)

/** R1 response bits */
#define SDCARD_R1_IDLE  (0x01U)
#define SDCARD_R1_ERROR (0x80U)

/** CMD8 argument: 2.7-3.6 V and the check pattern, the card echoes it */
#define SDCARD_CMD8_ARG (0x1AAU)

/** ACMD41 argument: the host supports high capacity cards */
#define SDCARD_ACMD41_HCS (1UL << 30)

/** OCR bit: card capacity status, the card is addressed by blocks */
#define SDCARD_OCR_CCS (0x40U)

/** Data tokens */
#define SDCARD_TOKEN_START     (0xFEU)
#define SDCARD_DATA_RESP_MASK  (0x1FU)
#define SDCARD_DATA_ACCEPTED   (0x05U)

/** Number of bytes to wait for the R1 response */
#define SDCARD_R1_RETRIES (10U)

/** Timeouts */
#define SDCARD_INIT_TIMEOUT_MS  (1000U)
#define SDCARD_READY_TIMEOUT_MS (500U)
#define SDCARD_TOKEN_TIMEOUT_MS (200U)

/** Card types */
typedef enum
{
    SDCARD_TYPE_NONE,
    /* Addressed by bytes */
    SDCARD_TYPE_SDSC,
    /* Addressed by blocks: SDHC, SDXC */
    SDCARD_TYPE_SDHC,
} SDCARD_Type_e;

static SDCARD_Type_e SDCARD_Type = SDCARD_TYPE_NONE;
static bool SDCARD_HwReady = false;

/**
 * @brief Check if a timeout passed
 * @param[in] Start Tick count at the start
 * @param[in] TimeoutMs Timeout
 * @return true if the time is over
 */
static bool SDCARD_TimeIsOver(TickType_t Start, uint32_t TimeoutMs)
{
    return (TickType_t)(xTaskGetTickCount() - Start) >= pdMS_TO_TICKS(TimeoutMs);
}

/**
 * @brief Wait until the card releases the data line (it holds it low while busy)
 * @param[in] TimeoutMs Timeout
 * @return true if ready
 */
static bool SDCARD_WaitReady(uint32_t TimeoutMs)
{
    TickType_t Start = xTaskGetTickCount();
    uint8_t Response;

    do
    {
        Response = BSP_SD_Exchange(0xFFU);
    } while ((Response != 0xFFU) && !SDCARD_TimeIsOver(Start, TimeoutMs));

    return Response == 0xFFU;
}

/**
 * @brief CS high, then one more byte: the card releases the data line only on a clock
 */
static void SDCARD_Deselect(void)
{
    BSP_SD_Select(false);
    (void)BSP_SD_Exchange(0xFFU);
}

/**
 * @brief CS low and wait until the card is ready
 * @return true if ready, the card is deselected otherwise
 */
static bool SDCARD_Select(void)
{
    bool Ready;

    BSP_SD_Select(true);
    (void)BSP_SD_Exchange(0xFFU);

    Ready = SDCARD_WaitReady(SDCARD_READY_TIMEOUT_MS);
    if (!Ready)
    {
        SDCARD_Deselect();
    }
    return Ready;
}

/**
 * @brief Send a command, the card stays selected for the data phase, the caller deselects it
 * @param[in] Command SDCARD_CMDx or SDCARD_ACMDx
 * @param[in] Argument Argument of the command
 * @return R1 response, bit 7 set on no answer
 */
static uint8_t SDCARD_Command(uint8_t Command, uint32_t Argument)
{
    uint8_t Index = (uint8_t)(Command & ~SDCARD_ACMD);
    uint8_t Response = 0xFFU;
    uint8_t Crc = 0x01U;
    uint8_t Retry = SDCARD_R1_RETRIES;
    bool Ready = true;

    if ((Command & SDCARD_ACMD) != 0U)
    {
        Response = SDCARD_Command(SDCARD_CMD55, 0U);
        Ready = (Response <= SDCARD_R1_IDLE);
    }

    if (Ready)
    {
        SDCARD_Deselect();
        Ready = SDCARD_Select();
    }

    if (Ready)
    {
        /* The CRC is checked only for CMD0 and CMD8 in SPI mode */
        if (Index == SDCARD_CMD0)
        {
            Crc = 0x95U;
        }
        else if (Index == SDCARD_CMD8)
        {
            Crc = 0x87U;
        }
        else
        {
            /* Any CRC with the stop bit */
        }

        (void)BSP_SD_Exchange((uint8_t)(0x40U | Index));
        (void)BSP_SD_Exchange((uint8_t)(Argument >> 24));
        (void)BSP_SD_Exchange((uint8_t)(Argument >> 16));
        (void)BSP_SD_Exchange((uint8_t)(Argument >> 8));
        (void)BSP_SD_Exchange((uint8_t)Argument);
        (void)BSP_SD_Exchange(Crc);

        do
        {
            Response = BSP_SD_Exchange(0xFFU);
            Retry--;
        } while (((Response & SDCARD_R1_ERROR) != 0U) && (Retry > 0U));
    }
    else
    {
        Response = 0xFFU;
    }
    return Response;
}

/**
 * @brief Repeat ACMD41 until the card leaves the idle state
 * @param[in] Argument Argument of ACMD41
 * @return true if the card is ready
 */
static bool SDCARD_WaitInitialized(uint32_t Argument)
{
    TickType_t Start = xTaskGetTickCount();
    uint8_t Response;

    do
    {
        Response = SDCARD_Command(SDCARD_ACMD41, Argument);
        if (Response != 0U)
        {
            vTaskDelay(1U);
        }
    } while ((Response != 0U) && !SDCARD_TimeIsOver(Start, SDCARD_INIT_TIMEOUT_MS));

    return Response == 0U;
}

/**
 * @brief Detect an SDv2 card after CMD8
 * @return Card type, SDCARD_TYPE_NONE on an error
 */
static SDCARD_Type_e SDCARD_InitV2(void)
{
    SDCARD_Type_e Type = SDCARD_TYPE_NONE;
    uint8_t Data[4];
    uint8_t Index;

    /* R7: the last two bytes echo the voltage and the check pattern */
    for (Index = 0U; Index < sizeof(Data); Index++)
    {
        Data[Index] = BSP_SD_Exchange(0xFFU);
    }

    if ((Data[2] == 0x01U) && (Data[3] == 0xAAU) && SDCARD_WaitInitialized(SDCARD_ACMD41_HCS)
        && (SDCARD_Command(SDCARD_CMD58, 0U) == 0U))
    {
        /* OCR */
        for (Index = 0U; Index < sizeof(Data); Index++)
        {
            Data[Index] = BSP_SD_Exchange(0xFFU);
        }
        Type = ((Data[0] & SDCARD_OCR_CCS) != 0U) ? SDCARD_TYPE_SDHC : SDCARD_TYPE_SDSC;
    }
    return Type;
}

/**
 * @brief Card address of a block
 * @param[in] Block Block number
 * @return Argument of the read and write commands
 */
static uint32_t SDCARD_Address(uint32_t Block)
{
    return (SDCARD_Type == SDCARD_TYPE_SDHC) ? Block : (Block * SDCARD_BLOCK_SIZE);
}

/**
 * @brief Receive a data block after a read command
 * @param[out] pBuffer Buffer of SDCARD_BLOCK_SIZE bytes
 * @return true if received
 */
static bool SDCARD_ReceiveBlock(uint8_t *pBuffer)
{
    TickType_t Start = xTaskGetTickCount();
    uint8_t Token;
    uint32_t Index;

    do
    {
        Token = BSP_SD_Exchange(0xFFU);
    } while ((Token == 0xFFU) && !SDCARD_TimeIsOver(Start, SDCARD_TOKEN_TIMEOUT_MS));

    if (Token == SDCARD_TOKEN_START)
    {
        for (Index = 0U; Index < SDCARD_BLOCK_SIZE; Index++)
        {
            pBuffer[Index] = BSP_SD_Exchange(0xFFU);
        }
        /* CRC, not checked */
        (void)BSP_SD_Exchange(0xFFU);
        (void)BSP_SD_Exchange(0xFFU);
    }
    return Token == SDCARD_TOKEN_START;
}

/**
 * @brief Send a data block after a write command
 * @param[in] pData Data of SDCARD_BLOCK_SIZE bytes
 * @return true if the card accepted it, it is busy with the programming after
 */
static bool SDCARD_TransmitBlock(const uint8_t *pData)
{
    uint8_t Response;
    uint32_t Index;

    (void)BSP_SD_Exchange(SDCARD_TOKEN_START);
    for (Index = 0U; Index < SDCARD_BLOCK_SIZE; Index++)
    {
        (void)BSP_SD_Exchange(pData[Index]);
    }
    /* Dummy CRC */
    (void)BSP_SD_Exchange(0xFFU);
    (void)BSP_SD_Exchange(0xFFU);

    Response = BSP_SD_Exchange(0xFFU);
    return (Response & SDCARD_DATA_RESP_MASK) == SDCARD_DATA_ACCEPTED;
}

/*****************************************************************************/
int32_t SDCARD_Init(void)
{
    SDCARD_Type_e Type = SDCARD_TYPE_NONE;
    uint8_t Index;
    int32_t Result = -ENODEV;

    if (!SDCARD_HwReady)
    {
        BSP_SD_Init();
        SDCARD_HwReady = true;
    }
    BSP_SD_SetSpeed(BSP_SD_SPEED_SLOW);

    /* At least 74 clocks with CS high wake up the card */
    BSP_SD_Select(false);
    for (Index = 0U; Index < 10U; Index++)
    {
        (void)BSP_SD_Exchange(0xFFU);
    }

    /* CMD0 with CS low puts the card into SPI mode */
    if (SDCARD_Command(SDCARD_CMD0, 0U) == SDCARD_R1_IDLE)
    {
        Result = -EIO;

        if (SDCARD_Command(SDCARD_CMD8, SDCARD_CMD8_ARG) == SDCARD_R1_IDLE)
        {
            Type = SDCARD_InitV2();
        }
        else if (SDCARD_WaitInitialized(0U))
        {
            /* SDv1 rejects CMD8, MMC rejects ACMD41 too and is not supported */
            Type = SDCARD_TYPE_SDSC;
        }
        else
        {
            /* No supported card */
        }

        /* A card addressed by bytes may have another default block length */
        if ((Type == SDCARD_TYPE_SDSC) && (SDCARD_Command(SDCARD_CMD16, SDCARD_BLOCK_SIZE) != 0U))
        {
            Type = SDCARD_TYPE_NONE;
        }
    }
    SDCARD_Deselect();

    SDCARD_Type = Type;
    if (Type != SDCARD_TYPE_NONE)
    {
        BSP_SD_SetSpeed(BSP_SD_SPEED_FAST);
        Result = 0;
    }
    return Result;
}

/*****************************************************************************/
bool SDCARD_IsAlive(void)
{
    bool Alive = false;

    if (SDCARD_Type != SDCARD_TYPE_NONE)
    {
        /* R2: R1 and a second status byte, both 0 when all is fine */
        Alive = (SDCARD_Command(SDCARD_CMD13, 0U) == 0U);
        Alive = (BSP_SD_Exchange(0xFFU) == 0U) && Alive;
        SDCARD_Deselect();

        if (!Alive)
        {
            SDCARD_Type = SDCARD_TYPE_NONE;
        }
    }
    return Alive;
}

/*****************************************************************************/
int32_t SDCARD_Read(uint8_t *pBuffer, uint32_t Block, uint32_t Count)
{
    int32_t Result = (SDCARD_Type != SDCARD_TYPE_NONE) ? 0 : -EIO;
    uint32_t Index;

    for (Index = 0U; (Index < Count) && (Result == 0); Index++)
    {
        if ((SDCARD_Command(SDCARD_CMD17, SDCARD_Address(Block + Index)) != 0U)
            || !SDCARD_ReceiveBlock(&pBuffer[Index * SDCARD_BLOCK_SIZE]))
        {
            Result = -EIO;
        }
        SDCARD_Deselect();
    }
    return Result;
}

/*****************************************************************************/
int32_t SDCARD_Write(const uint8_t *pData, uint32_t Block, uint32_t Count)
{
    int32_t Result = (SDCARD_Type != SDCARD_TYPE_NONE) ? 0 : -EIO;
    uint32_t Index;

    for (Index = 0U; (Index < Count) && (Result == 0); Index++)
    {
        if ((SDCARD_Command(SDCARD_CMD24, SDCARD_Address(Block + Index)) != 0U)
            || !SDCARD_TransmitBlock(&pData[Index * SDCARD_BLOCK_SIZE]))
        {
            Result = -EIO;
        }
        SDCARD_Deselect();
    }
    return Result;
}

/*****************************************************************************/
int32_t SDCARD_Sync(void)
{
    int32_t Result = -EIO;

    if (SDCARD_Select())
    {
        SDCARD_Deselect();
        Result = 0;
    }
    return Result;
}
