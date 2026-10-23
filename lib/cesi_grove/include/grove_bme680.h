#ifndef GROVE_BME680_H
#define GROVE_BME680_H

#include <stdbool.h>
#include "cesi_types.h"

bool GroveBME680_Init(void);
bool GroveBME680_ReadChipId(uint8_t *chip_id);

/* Étape 1 : mesure brute de la température (un nombre de 20 bits fourni par la puce) */
bool GroveBME680_ReadRawTemperature(uint32_t *adc);

/* Étape 2 : conversion du nombre brut en degrés Celsius (formule de calibration Bosch) */
float GroveBME680_CompensateTemperature(uint32_t adc);

/* Auto-test de la formule avec des coefficients d'exemple : doit renvoyer 24.13 (°C) */
float GroveBME680_TestFormula(void);

/* Mesure complète : pour l'instant, seule la température est calculée */
bool GroveBME680_ReadEnvironment(Env_Data *data);

#endif
