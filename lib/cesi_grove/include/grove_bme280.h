#ifndef GROVE_BME280_H
#define GROVE_BME280_H

#include <stdbool.h>
#include <stdint.h>
#include "cesi_types.h"

/* Trouve le capteur (0x76 ou 0x77), le réinitialise et lit ses coefficients de calibration */
bool GroveBME280_Init(void);

/* Une mesure complète : température (°C), humidité (%) et pression (hPa) */
bool GroveBME280_ReadEnvironment(Env_Data *data);

#endif
