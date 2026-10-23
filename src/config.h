#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#define FW_VERSION  "1.0.0"
#define FW_LOT      "WWW-G3-2026-001"

typedef struct
{
    int32_t log_interval;/* minutes entre deux mesures */
    int32_t file_max_size;/* octets avant archivage du fichier */
    int32_t timeout;/* secondes avant d'abandonner un capteur */
    int32_t lumin;/* capteur de luminosité actif (1) ou non (0) */
    int32_t lumin_low;/* en dessous = faible */
    int32_t lumin_high;/* au-dessus = forte */
    int32_t temp_air;
    int32_t min_temp_air;/* température air minimum et en dessous = erreur */
    int32_t max_temp_air;/* au-dessus = erreur */
    int32_t hygr;
    int32_t hygr_mint;/* en dessous, humidité non retenue */
    int32_t hygr_maxt;/* au-dessus, humidité non retenue */
    int32_t pressure;
    int32_t pressure_min;/*en dessous = erreur */
    int32_t pressure_max;/* au-dessus = erreur */
} Config;

extern Config g_config;

void Config_Load(void);
void Config_SetDefaults(void);
bool Config_Save(void);
void Config_HandleCommand(char *line);

#endif
