#include "sd_logger.h"
#include "board_pins.h"
#include "grove_rtc_ds1307.h"
#include "ff.h"
#include "diskio.h"
#include <stdio.h>
#include <string.h>

#define SD_CMD0     0
#define SD_CMD1     1
#define SD_CMD8     8
#define SD_CMD9     9
#define SD_CMD16    16
#define SD_CMD17    17
#define SD_CMD24    24
#define SD_CMD55    55
#define SD_CMD58    58
#define SD_ACMD41   41

#define SD_R1_IDLE_STATE      0x01U
#define SD_R1_ILLEGAL_COMMAND 0x04U

#define SD_TOKEN_START_BLOCK  0xFEU
#define SD_DATA_RESP_MASK     0x1FU
#define SD_DATA_RESP_ACCEPTED 0x05U

#define SD_SECTOR_SIZE      512U
#define SD_INIT_TIMEOUT_MS  1000U
#define SD_POWERUP_DELAY_MS 50U
#define SD_READ_TIMEOUT_MS  200U
#define SD_WRITE_TIMEOUT_MS 500U

/* Apres l'init (<= 400 kHz impose), on monte a 16 MHz / 4 = 4 MHz. */
#define SD_PRESCALER_INIT SPI_BAUDRATEPRESCALER_256
#define SD_PRESCALER_FAST SPI_BAUDRATEPRESCALER_4

extern SPI_HandleTypeDef SD_SPI_HANDLE;

typedef enum { SDLOGGER_CARD_UNKNOWN = 0, SDLOGGER_CARD_SDSC, SDLOGGER_CARD_SDHC } SDLoggerCardType;

static SDLoggerCardType g_card_type = SDLOGGER_CARD_UNKNOWN;
static DSTATUS g_disk_status = STA_NOINIT;
static FATFS g_fs;
static bool g_ready = false;

static uint8_t SD_SPI_Transfer(uint8_t tx)
{
    uint8_t rx = 0xFF;
    (void)HAL_SPI_TransmitReceive(&SD_SPI_HANDLE, &tx, &rx, 1, 100);
    return rx;
}

static void SD_Deselect(void)
{
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET);
    /* Un octet d'horloge apres CS haut libere la sortie MISO de la carte. */
    (void)SD_SPI_Transfer(0xFF);
}

static void SD_Select(void)
{
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_RESET);
}

static void SD_SetPrescaler(uint32_t prescaler)
{
    SD_SPI_HANDLE.Init.BaudRatePrescaler = prescaler;
    (void)HAL_SPI_Init(&SD_SPI_HANDLE);
}

static bool SD_WaitReady(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        if (SD_SPI_Transfer(0xFF) == 0xFF)
        {
            return true;
        }
    }

    return false;
}

/* Laisse CS bas : l'appelant lit la suite de la reponse puis appelle SD_Deselect. */
static uint8_t SD_SendCommand(uint8_t cmd, uint32_t arg, uint8_t crc)
{
    uint8_t response = 0xFF;
    uint8_t frame[6];
    uint8_t i;

    SD_Deselect();
    SD_Select();

    if (cmd != SD_CMD0)
    {
        if (!SD_WaitReady(50U))
        {
            SD_Deselect();
            return 0xFF;
        }
    }

    frame[0] = (uint8_t)(0x40U | cmd);
    frame[1] = (uint8_t)(arg >> 24);
    frame[2] = (uint8_t)(arg >> 16);
    frame[3] = (uint8_t)(arg >> 8);
    frame[4] = (uint8_t)(arg);
    frame[5] = crc;

    for (i = 0; i < 6; ++i)
    {
        (void)SD_SPI_Transfer(frame[i]);
    }

    for (i = 0; i < 10; ++i)
    {
        response = SD_SPI_Transfer(0xFF);
        if ((response & 0x80U) == 0U)
        {
            return response;
        }
    }

    return response;
}

/* Init SPI de la carte (CMD0/CMD8/ACMD41/CMD58), puis passage en vitesse rapide. */
static bool SD_CardInit(void)
{
    uint8_t i;
    uint8_t r1;
    uint8_t r7[4];
    uint8_t ocr[4];
    uint32_t start;
    uint8_t cmd0_try;

    g_card_type = SDLOGGER_CARD_UNKNOWN;

    SD_SetPrescaler(SD_PRESCALER_INIT);
    SD_Deselect();
    HAL_Delay(SD_POWERUP_DELAY_MS);

    /* Au moins 74 coups d'horloge CS haut, exige par le mode SPI. */
    for (i = 0; i < 10; ++i)
    {
        (void)SD_SPI_Transfer(0xFF);
    }

    r1 = 0xFFU;
    for (cmd0_try = 0; cmd0_try < 32U; ++cmd0_try)
    {
        r1 = SD_SendCommand(SD_CMD0, 0x00000000UL, 0x95U);
        SD_Deselect();

        if (r1 == SD_R1_IDLE_STATE)
        {
            break;
        }

        HAL_Delay(2U);
    }

    if (r1 != SD_R1_IDLE_STATE)
    {
        return false;
    }

    r1 = SD_SendCommand(SD_CMD8, 0x000001AAUL, 0x87U);
    if (r1 == SD_R1_IDLE_STATE)
    {
        for (i = 0; i < 4; ++i)
        {
            r7[i] = SD_SPI_Transfer(0xFF);
        }
        SD_Deselect();

        if ((r7[2] != 0x01U) || (r7[3] != 0xAAU))
        {
            return false;
        }

        start = HAL_GetTick();
        do
        {
            r1 = SD_SendCommand(SD_CMD55, 0x00000000UL, 0x01U);
            SD_Deselect();
            if (r1 > 0x01U)
            {
                break;
            }

            r1 = SD_SendCommand(SD_ACMD41, 0x40000000UL, 0x01U);
            SD_Deselect();
            if (r1 == 0x00U)
            {
                break;
            }
        }
        while ((HAL_GetTick() - start) < SD_INIT_TIMEOUT_MS);

        if (r1 != 0x00U)
        {
            return false;
        }

        r1 = SD_SendCommand(SD_CMD58, 0x00000000UL, 0x01U);
        if (r1 != 0x00U)
        {
            SD_Deselect();
            return false;
        }

        for (i = 0; i < 4; ++i)
        {
            ocr[i] = SD_SPI_Transfer(0xFF);
        }
        SD_Deselect();

        g_card_type = ((ocr[0] & 0x40U) != 0U) ? SDLOGGER_CARD_SDHC : SDLOGGER_CARD_SDSC;
    }
    else
    {
        if ((r1 & SD_R1_ILLEGAL_COMMAND) == 0U)
        {
            SD_Deselect();
            return false;
        }
        SD_Deselect();

        start = HAL_GetTick();
        do
        {
            r1 = SD_SendCommand(SD_CMD1, 0x00000000UL, 0x01U);
            SD_Deselect();
        }
        while ((r1 != 0x00U) && ((HAL_GetTick() - start) < SD_INIT_TIMEOUT_MS));

        if (r1 != 0x00U)
        {
            return false;
        }

        g_card_type = SDLOGGER_CARD_SDSC;
    }

    if (g_card_type == SDLOGGER_CARD_SDSC)
    {
        /* FatFs suppose des blocs de 512 octets ; une SDSC peut avoir un autre defaut. */
        r1 = SD_SendCommand(SD_CMD16, SD_SECTOR_SIZE, 0x01U);
        SD_Deselect();
        if (r1 != 0x00U)
        {
            return false;
        }
    }

    SD_SetPrescaler(SD_PRESCALER_FAST);
    return true;
}

/* ---------- lecture / ecriture de blocs ---------------------------------- */

/* SDSC adresse en octets, SDHC en blocs. */
static uint32_t SD_BlockArg(uint32_t sector)
{
    return (g_card_type == SDLOGGER_CARD_SDHC) ? sector : (sector * SD_SECTOR_SIZE);
}

/* Attend le jeton de debut de bloc puis lit len octets + 2 octets de CRC ignores. CS reste bas. */
static bool SD_ReadDataBlock(uint8_t *buff, uint16_t len)
{
    uint32_t start = HAL_GetTick();
    uint8_t token;

    do
    {
        token = SD_SPI_Transfer(0xFF);
    }
    while ((token == 0xFFU) && ((HAL_GetTick() - start) < SD_READ_TIMEOUT_MS));

    if (token != SD_TOKEN_START_BLOCK)
    {
        return false;
    }

    /* MOSI doit rester a 1 pendant la lecture : on envoie le buffer deja rempli de 0xFF. */
    memset(buff, 0xFF, len);
    if (HAL_SPI_TransmitReceive(&SD_SPI_HANDLE, buff, buff, len, 1000U) != HAL_OK)
    {
        return false;
    }

    (void)SD_SPI_Transfer(0xFF);
    (void)SD_SPI_Transfer(0xFF);
    return true;
}

static bool SD_ReadSector(uint8_t *buff, uint32_t sector)
{
    bool ok = false;

    if (SD_SendCommand(SD_CMD17, SD_BlockArg(sector), 0x01U) == 0x00U)
    {
        ok = SD_ReadDataBlock(buff, (uint16_t)SD_SECTOR_SIZE);
    }
    SD_Deselect();
    return ok;
}

static bool SD_WriteSector(const uint8_t *buff, uint32_t sector)
{
    bool ok = false;

    if (SD_SendCommand(SD_CMD24, SD_BlockArg(sector), 0x01U) == 0x00U)
    {
        (void)SD_SPI_Transfer(0xFF);
        (void)SD_SPI_Transfer(SD_TOKEN_START_BLOCK);
        if (HAL_SPI_Transmit(&SD_SPI_HANDLE, (uint8_t *)buff, (uint16_t)SD_SECTOR_SIZE, 1000U) == HAL_OK)
        {
            (void)SD_SPI_Transfer(0xFF); /* CRC factice, non verifie en mode SPI */
            (void)SD_SPI_Transfer(0xFF);
            ok = ((SD_SPI_Transfer(0xFF) & SD_DATA_RESP_MASK) == SD_DATA_RESP_ACCEPTED)
                 && SD_WaitReady(SD_WRITE_TIMEOUT_MS);
        }
    }
    SD_Deselect();
    return ok;
}

static bool SD_GetSectorCount(DWORD *count)
{
    uint8_t csd[16];
    uint32_t c_size;
    uint32_t shift;

    if ((SD_SendCommand(SD_CMD9, 0x00000000UL, 0x01U) != 0x00U) || !SD_ReadDataBlock(csd, sizeof(csd)))
    {
        SD_Deselect();
        return false;
    }
    SD_Deselect();

    if ((csd[0] >> 6) == 1U) /* CSD v2 (SDHC/SDXC) */
    {
        c_size = ((uint32_t)(csd[7] & 0x3FU) << 16) | ((uint32_t)csd[8] << 8) | csd[9];
        *count = (c_size + 1U) << 10;
    }
    else /* CSD v1 (SDSC) : (C_SIZE+1) * 2^(C_SIZE_MULT+2) blocs de 2^READ_BL_LEN octets */
    {
        c_size = ((uint32_t)(csd[6] & 0x03U) << 10) | ((uint32_t)csd[7] << 2) | (csd[8] >> 6);
        shift = ((uint32_t)(csd[9] & 0x03U) << 1) + (csd[10] >> 7) + 2U + (csd[5] & 0x0FU) - 9U;
        *count = (c_size + 1U) << shift;
    }
    return true;
}

/* ---------- couche disque pour FatFs ------------------------------------- */

DSTATUS disk_status(BYTE pdrv)
{
    return (pdrv == 0U) ? g_disk_status : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0U)
    {
        return STA_NOINIT;
    }

    g_disk_status = SD_CardInit() ? 0 : STA_NOINIT;
    return g_disk_status;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
    if ((pdrv != 0U) || (count == 0U))
    {
        return RES_PARERR;
    }
    if ((g_disk_status & STA_NOINIT) != 0)
    {
        return RES_NOTRDY;
    }

    /* Un CMD17 par secteur : plus lent que CMD18 mais bien plus simple. */
    for (; count > 0U; --count, ++sector, buff += SD_SECTOR_SIZE)
    {
        if (!SD_ReadSector(buff, sector))
        {
            return RES_ERROR;
        }
    }
    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
    if ((pdrv != 0U) || (count == 0U))
    {
        return RES_PARERR;
    }
    if ((g_disk_status & STA_NOINIT) != 0)
    {
        return RES_NOTRDY;
    }

    for (; count > 0U; --count, ++sector, buff += SD_SECTOR_SIZE)
    {
        if (!SD_WriteSector(buff, sector))
        {
            return RES_ERROR;
        }
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0U)
    {
        return RES_PARERR;
    }
    if ((g_disk_status & STA_NOINIT) != 0)
    {
        return RES_NOTRDY;
    }

    switch (cmd)
    {
    case CTRL_SYNC:
        /* Chaque ecriture attend deja la fin de programmation de la carte. */
        return SD_WaitReady(SD_WRITE_TIMEOUT_MS) ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT:
        return SD_GetSectorCount((DWORD *)buff) ? RES_OK : RES_ERROR;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1U; /* taille d'effacement inconnue */
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

DWORD get_fattime(void)
{
    RTC_DateTime dt;

    if (!GroveRTC_GetDateTime(&dt) || (dt.year < 1980U))
    {
        dt.year = 2026U;
        dt.month = 1U;
        dt.day = 1U;
        dt.hours = 0U;
        dt.minutes = 0U;
        dt.seconds = 0U;
    }

    return ((DWORD)(dt.year - 1980U) << 25) | ((DWORD)dt.month << 21) | ((DWORD)dt.day << 16)
           | ((DWORD)dt.hours << 11) | ((DWORD)dt.minutes << 5) | ((DWORD)dt.seconds >> 1);
}

/* ---------- journal : fichiers YYMMDD_n.LOG ------------------------------ */

/* Sans LFN le nom est limite a 8 caracteres : "YYMMDD_n" laisse un seul chiffre de revision. */
#define SDLOG_MAX_REVISION 9U
#define SDLOG_NAME_SIZE    16U /* "YYMMDD_n.LOG" (12) + '\0' ; marge pour l'analyse de troncature de gcc */

bool SDLogger_Init(void)
{
    g_ready = false;

    if (disk_initialize(0U) != 0)
    {
        return false;
    }

    g_ready = (f_mount(&g_fs, "", 1U) == FR_OK);
    return g_ready;
}

void SDLogger_Unmount(void)
{
    (void)f_mount(NULL, "", 0U);
    g_disk_status = STA_NOINIT;
    g_ready = false;
}

bool SDLogger_IsReady(void)
{
    return g_ready;
}

static void SDLog_BuildName(char *name, const RTC_DateTime *now, unsigned revision)
{
    (void)snprintf(name, SDLOG_NAME_SIZE, "%02u%02u%02u_%u.LOG", (unsigned)(now->year % 100U),
                   (unsigned)(now->month % 100U), (unsigned)(now->day % 100U), revision % 10U);
}

static SDLogStatus SDLog_FromFResult(FRESULT res)
{
    DWORD free_clusters;
    FATFS *fs;

    if (res == FR_DENIED)
    {
        return SDLOG_ERR_FULL;
    }
    if ((f_getfree("", &free_clusters, &fs) == FR_OK) && (free_clusters == 0U))
    {
        return SDLOG_ERR_FULL;
    }
    return SDLOG_ERR_IO;
}

/* Renomme le fichier courant en YYMMDD_n avec le premier n libre. */
static SDLogStatus SDLog_Archive(const RTC_DateTime *now, const char *current)
{
    char archive[SDLOG_NAME_SIZE];
    FILINFO info;
    unsigned revision;
    FRESULT res;

    for (revision = 1U; revision <= SDLOG_MAX_REVISION; ++revision)
    {
        SDLog_BuildName(archive, now, revision);
        res = f_stat(archive, &info);
        if (res == FR_NO_FILE)
        {
            return (f_rename(current, archive) == FR_OK) ? SDLOG_OK : SDLOG_ERR_IO;
        }
        if (res != FR_OK)
        {
            return SDLOG_ERR_IO;
        }
    }

    /* Toutes les revisions du jour sont prises : on ne peut plus archiver. */
    return SDLOG_ERR_FULL;
}

/* Ecrit len octets ; une ecriture courte est un disque plein. */
static FRESULT SDLog_Write(FIL *file, const char *data, UINT len)
{
    UINT written = 0U;
    FRESULT res = f_write(file, data, len, &written);

    return ((res == FR_OK) && (written != len)) ? FR_DENIED : res;
}

SDLogStatus SDLogger_WriteLine(const RTC_DateTime *now, const char *line, uint32_t max_size)
{
    char name[SDLOG_NAME_SIZE];
    FILINFO info;
    FIL file;
    FRESULT res;
    FRESULT close_res;
    SDLogStatus status;
    UINT line_len;

    if (!g_ready || (now == NULL) || (line == NULL))
    {
        return SDLOG_ERR_IO;
    }

    SDLog_BuildName(name, now, 0U);
    line_len = (UINT)strlen(line);

    res = f_stat(name, &info);
    if (res == FR_NO_FILE)
    {
        info.fsize = 0U;
    }
    else if (res != FR_OK)
    {
        return SDLOG_ERR_IO;
    }

    /* fsize > 0 : une ligne plus grande que max_size a elle seule ne doit pas archiver un fichier vide. */
    if ((info.fsize > 0U) && ((info.fsize + line_len + 2U) > max_size))
    {
        status = SDLog_Archive(now, name);
        if (status != SDLOG_OK)
        {
            return status;
        }
    }

    res = f_open(&file, name, FA_OPEN_ALWAYS | FA_WRITE);
    if (res != FR_OK)
    {
        return SDLog_FromFResult(res);
    }

    res = f_lseek(&file, f_size(&file));
    if (res == FR_OK)
    {
        res = SDLog_Write(&file, line, line_len);
    }
    if (res == FR_OK)
    {
        res = SDLog_Write(&file, "\r\n", 2U);
    }

    /* f_close ecrit le secteur courant et le repertoire : les donnees survivent a une coupure. */
    close_res = f_close(&file);
    if (res == FR_OK)
    {
        res = close_res;
    }

    return (res == FR_OK) ? SDLOG_OK : SDLog_FromFResult(res);
}
