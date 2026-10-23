#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include <stdbool.h>
#include <stdint.h>
#include "cesi_types.h"

typedef enum
{
	SDLOG_OK = 0,
	SDLOG_ERR_FULL,
	SDLOG_ERR_IO
} SDLogStatus;

bool SDLogger_Init(void);      /* prépare la carte et le système de fichiers ; true si utilisable */
void SDLogger_Unmount(void);   /* plus aucune écriture : la carte peut être retirée */
bool SDLogger_IsReady(void);

/* Ajoute une ligne (suivie de "\r\n") au fichier du jour AAMMJJ_0.LOG, avec archivage si besoin */
SDLogStatus SDLogger_WriteLine(const RTC_DateTime *now, const char *line, uint32_t max_size);

#endif
