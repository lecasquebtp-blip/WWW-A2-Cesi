#include "config.h"
#include "board_pins.h"
#include "grove_rtc_ds1307.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define CONFIG_FLASH_ADDR   0x08060000U
#define CONFIG_MAGIC        0x57575702U
Config g_config;

typedef struct
{
    uint32_t magic;
    Config   cfg;
} StoredConfig;

typedef struct
{
    const char *name;
    int32_t    *value;
    int32_t     min;
    int32_t     max;
} Param;

static const Param params[] = {
    {"LOG_INTERVAL",  &g_config.log_interval,  1, 1440},
    {"FILE_MAX_SIZE", &g_config.file_max_size, 256, 65535},
    {"TIMEOUT",       &g_config.timeout,       1, 300},
    {"LUMIN",         &g_config.lumin,         0, 1},
    {"LUMIN_LOW",     &g_config.lumin_low,     0, 1023},
    {"LUMIN_HIGH",    &g_config.lumin_high,    0, 1023},
    {"TEMP_AIR",      &g_config.temp_air,      0, 1},
    {"MIN_TEMP_AIR",  &g_config.min_temp_air,  -40, 85},
    {"MAX_TEMP_AIR",  &g_config.max_temp_air,  -40, 85},
    {"HYGR",          &g_config.hygr,          0, 1},
    {"HYGR_MINT",     &g_config.hygr_mint,     -40, 85},
    {"HYGR_MAXT",     &g_config.hygr_maxt,     -40, 85},
    {"PRESSURE",      &g_config.pressure,      0, 1},
    {"PRESSURE_MIN",  &g_config.pressure_min,  300, 1100},
    {"PRESSURE_MAX",  &g_config.pressure_max,  300, 1100},
};

#define NB_PARAMS (sizeof(params) / sizeof(params[0]))

void Config_SetDefaults(void)
{
    g_config.log_interval  = 10;
    g_config.file_max_size = 2048;
    g_config.timeout       = 30;
    g_config.lumin         = 1;
    g_config.lumin_low     = 255;
    g_config.lumin_high    = 768;
    g_config.temp_air      = 1;
    g_config.min_temp_air  = -10;
    g_config.max_temp_air  = 60;
    g_config.hygr          = 1;
    g_config.hygr_mint     = 0;
    g_config.hygr_maxt     = 50;
    g_config.pressure      = 1;
    g_config.pressure_min  = 850;
    g_config.pressure_max  = 1080;
}

void Config_Load(void)
{
    const StoredConfig *stored = (const StoredConfig *)CONFIG_FLASH_ADDR;

    if (stored->magic == CONFIG_MAGIC)
        g_config = stored->cfg;
    else
        Config_SetDefaults();
}

bool Config_Save(void)
{
    StoredConfig stored = {CONFIG_MAGIC, g_config};
    const uint32_t *words = (const uint32_t *)&stored;
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_error = 0;
    bool ok;

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Sector = FLASH_SECTOR_7;
    erase.NbSectors = 1;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    HAL_FLASH_Unlock();
    ok = HAL_FLASHEx_Erase(&erase, &sector_error) == HAL_OK;

    for (unsigned int i = 0; ok && i < sizeof(stored) / 4; i++)
        ok = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, CONFIG_FLASH_ADDR + 4 * i, words[i]) == HAL_OK;

    HAL_FLASH_Lock();
    return ok;
}

static bool set_rtc(const char *name, const char *value)
{
    const char *days[] = {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};
    RTC_DateTime dt;
    unsigned int a, b, c;

    if (!GroveRTC_GetDateTime(&dt))
        return false;

    if (strcmp(name, "CLOCK") == 0)
    {
        if (sscanf(value, "%u:%u:%u", &a, &b, &c) != 3 || a > 23 || b > 59 || c > 59)
            return false;
        dt.hours = a;
        dt.minutes = b;
        dt.seconds = c;
    }
    else if (strcmp(name, "DATE") == 0)
    {
        if (sscanf(value, "%u,%u,%u", &a, &b, &c) != 3 || a < 1 || a > 12 || b < 1 || b > 31
            || c < 2000 || c > 2099)
            return false;
        dt.month = a;
        dt.day = b;
        dt.year = c;
    }
    else
    {
        int d = 0;
        while (d < 7 && strcmp(value, days[d]) != 0)
            d++;
        if (d == 7)
            return false;
        dt.dayOfWeek = d + 1;
    }

    return GroveRTC_SetDateTime(&dt);
}

void Config_HandleCommand(char *line)
{
    for (char *p = line; *p; p++)
        *p = toupper((unsigned char)*p);
    char *value = strchr(line, '=');
    if (value != 0)
    {
        *value = '\0';
        value++;
    }

    if (strcmp(line, "VERSION") == 0)
    {
        printf("WWW version %s, lot %s\r\n", FW_VERSION, FW_LOT);
        return;
    }

    if (strcmp(line, "RESET") == 0)
    {
        Config_SetDefaults();
        printf(Config_Save() ? "Parametres par defaut\r\n" : "Erreur d'ecriture flash\r\n");
        return;
    }

    if (strcmp(line, "SHOW") == 0 || strcmp(line, "HELP") == 0)
    {
        for (unsigned int i = 0; i < NB_PARAMS; i++)
            printf("  %s=%ld\r\n", params[i].name, (long)*params[i].value);
        printf("  Autres : RESET, VERSION, CLOCK=HH:MM:SS, DATE=MM,JJ,AAAA, DAY=MON..SUN\r\n");
        return;
    }

    if (value == 0)
    {
        printf("Commande inconnue (SHOW pour la liste)\r\n");
        return;
    }

    if (strcmp(line, "CLOCK") == 0 || strcmp(line, "DATE") == 0 || strcmp(line, "DAY") == 0)
    {
        printf(set_rtc(line, value) ? "OK\r\n" : "Valeur invalide ou horloge RTC absente\r\n");
        return;
    }

    if (strcmp(line, "LOG_INTERVALL") == 0)
        strcpy(line, "LOG_INTERVAL");

    for (unsigned int i = 0; i < NB_PARAMS; i++)
    {
        if (strcmp(line, params[i].name) != 0)
            continue;

        char *end;
        long v = strtol(value, &end, 10);
        if (value[0] == '\0' || *end != '\0' || v < params[i].min || v > params[i].max)
        {
            printf("Valeur pas valide: %s attend %ld a %ld\r\n", params[i].name,
                   (long)params[i].min, (long)params[i].max);
            return;
        }

        *params[i].value = v;
        if (Config_Save())
            printf("OK %s=%ld\r\n", params[i].name, v);
        else
            printf("Erreur d'ecriture flash\r\n");
        return;
    }

    printf("Parametre inconnu: %s (SHOW pour la liste)\r\n", line);
}
