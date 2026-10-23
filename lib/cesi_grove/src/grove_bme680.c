#include "grove_bme680.h"
#include "board_pins.h"

extern I2C_HandleTypeDef GROVE_I2C_HANDLE;

/* Registres du BME680 (datasheet Bosch) */
#define BME680_REG_STATUS     0x1D    /* meas_status_0 : bit 7 = nouvelle mesure disponible */
#define BME680_REG_TEMP_MSB   0x22    /* température brute : 3 octets (20 bits) */
#define BME680_REG_CTRL_HUM   0x72    /* oversampling de l'humidité */
#define BME680_REG_CTRL_MEAS  0x74    /* oversampling température/pression + mode */
#define BME680_REG_T2_LSB     0x8A    /* calibration : par_t2 (2 octets) puis par_t3 (1 octet) */
#define BME680_REG_ID         0xD0    /* identifiant de la puce */
#define BME680_REG_RESET      0xE0    /* on y écrit 0xB6 pour réinitialiser la puce */
#define BME680_REG_T1_LSB     0xE9    /* calibration : par_t1 (2 octets) */

#define BME680_CHIP_ID        0x61
#define BME680_RESET_CMD      0xB6
#define BME680_MODE_MASK      0x03    /* bits 1:0 de ctrl_meas : 00 = veille, 01 = mesure unique */
#define BME680_NEW_DATA       0x80    /* bit 7 de meas_status_0 */

#define BME680_ADDR_DEFAULT   (0x76 << 1)

static uint16_t bme680_addr = BME680_ADDR_DEFAULT;

/* Coefficients de calibration de la température : propres à chaque puce, lus à l'initialisation */
static uint16_t par_t1;
static int16_t  par_t2;
static int8_t   par_t3;

/* Résultat intermédiaire de la formule, aussi utilisé plus tard pour la pression et l'humidité */
static float t_fine;

static bool read_reg(uint8_t reg, uint8_t *data, uint16_t len)
{
    if (HAL_I2C_Master_Transmit(&GROVE_I2C_HANDLE, bme680_addr, &reg, 1, 100) != HAL_OK)
        return false;

    return HAL_I2C_Master_Receive(&GROVE_I2C_HANDLE, bme680_addr, data, len, 100) == HAL_OK;
}

static bool write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    return HAL_I2C_Master_Transmit(&GROVE_I2C_HANDLE, bme680_addr, data, 2, 100) == HAL_OK;
}

bool GroveBME680_ReadChipId(uint8_t *chip_id)
{
    if (chip_id == 0) return false;
    return read_reg(BME680_REG_ID, chip_id, 1);
}

/* Lit les trois coefficients de calibration de la température. */
static bool read_calibration(void)
{
    uint8_t t2_t3[3];   /* 0x8A, 0x8B = par_t2 (poids faible, poids fort) ; 0x8C = par_t3 */
    uint8_t t1[2];      /* 0xE9, 0xEA = par_t1 (poids faible, poids fort) */

    if (!read_reg(BME680_REG_T2_LSB, t2_t3, 3)) return false;
    if (!read_reg(BME680_REG_T1_LSB, t1, 2))    return false;

    par_t2 = (int16_t)(((uint16_t)t2_t3[1] << 8) | t2_t3[0]);
    par_t3 = (int8_t)t2_t3[2];
    par_t1 = (uint16_t)(((uint16_t)t1[1] << 8) | t1[0]);

    return true;
}

bool GroveBME680_Init(void)
{
    uint8_t id = 0;

    /* Adresse Seeed : 0x76 par défaut, parfois 0x77 */
    bme680_addr = (0x76 << 1);
    if (!(GroveBME680_ReadChipId(&id) && id == BME680_CHIP_ID))
    {
        bme680_addr = (0x77 << 1);
        if (!(GroveBME680_ReadChipId(&id) && id == BME680_CHIP_ID))
            return false;
    }

    /* On remet la puce à zéro, puis on lit ses coefficients de calibration */
    if (!write_reg(BME680_REG_RESET, BME680_RESET_CMD))
        return false;
    HAL_Delay(10);

    return read_calibration();
}

bool GroveBME680_ReadRawTemperature(uint32_t *adc)
{
    if (adc == 0) return false;

    /* Humidité ignorée ; température x2, pression ignorée, mode "mesure unique" */
    if (!write_reg(BME680_REG_CTRL_HUM, 0x00)) return false;
    if (!write_reg(BME680_REG_CTRL_MEAS, (2U << 5) | (0U << 2) | 0x01U)) return false;

    /* On attend que la puce soit revenue en veille : la mesure est alors terminée */
    uint8_t ctrl = 0;
    uint32_t debut = HAL_GetTick();
    do
    {
        if (!read_reg(BME680_REG_CTRL_MEAS, &ctrl, 1)) return false;
        if ((ctrl & BME680_MODE_MASK) == 0) break;
    } while ((HAL_GetTick() - debut) < 300);

    if ((ctrl & BME680_MODE_MASK) != 0) return false;      /* délai dépassé */

    /* Une nouvelle mesure est-elle disponible ? */
    uint8_t status = 0;
    if (!read_reg(BME680_REG_STATUS, &status, 1) || !(status & BME680_NEW_DATA))
        return false;

    /* 3 octets : bits 19..12, bits 11..4, puis bits 3..0 dans les 4 bits de poids fort du dernier */
    uint8_t raw[3];
    if (!read_reg(BME680_REG_TEMP_MSB, raw, 3)) return false;

    *adc = ((uint32_t)raw[0] << 12) | ((uint32_t)raw[1] << 4) | ((uint32_t)raw[2] >> 4);
    return true;
}

/*
 * Conversion du nombre brut du capteur en degrés Celsius.
 *
 * Formule de Bosch (version "flottant" du pilote officiel du BME680) :
 *
 *     var1   = (adc / 16384  - par_t1 / 1024) * par_t2
 *     var2   = (adc / 131072 - par_t1 / 8192) * (adc / 131072 - par_t1 / 8192) * (par_t3 * 16)
 *     t_fine = var1 + var2
 *     température en °C = t_fine / 5120
 *
 * Les nombres 1024, 8192, 16... sont des constantes de la formule : ils ne changent jamais.
 * par_t1, par_t2, par_t3 sont des DONNÉES de calibration, propres à chaque puce.
 *
 * Piège classique : en C, 495100 / 16384 est une division ENTIÈRE (le résultat est tronqué).
 * On convertit donc en float avant de diviser, par exemple avec 16384.0f.
 */
float GroveBME680_CompensateTemperature(uint32_t adc)
{
    float var1 = ((float)adc / 16384.0f - (float)par_t1 / 1024.0f) * (float)par_t2;
    float a    = (float)adc / 131072.0f - (float)par_t1 / 8192.0f;
    float var2 = a * a * ((float)par_t3 * 16.0f);
    t_fine = var1 + var2;

    return t_fine / 5120.0f;
}

/*
 * Auto-test de la formule, sans capteur : on utilise des coefficients d'exemple
 * (par_t1 = 26134, par_t2 = 26294, par_t3 = 3) et on vérifie la température obtenue.
 * À appeler AVANT GroveBME680_Init(), qui recharge ensuite les vrais coefficients de la puce.
 *
 *     adc = 480000  ->  19.39 °C
 *     adc = 495100  ->  24.13 °C   (24.125 arrondi)
 *     adc = 510000  ->  28.80 °C
 */
float GroveBME680_TestFormula(void)
{
    par_t1 = 26134;
    par_t2 = 26294;
    par_t3 = 3;

    return GroveBME680_CompensateTemperature(495100);
}

bool GroveBME680_ReadEnvironment(Env_Data *data)
{
    if (data == 0) return false;

    uint32_t adc = 0;
    if (!GroveBME680_ReadRawTemperature(&adc))
        return false;

    data->temperature_c = GroveBME680_CompensateTemperature(adc);

    /* Pas encore calculés : humidité, pression et gaz (même méthode que la température) */
    data->humidity_percent = 0.0f;
    data->pressure_hpa = 0.0f;
    data->gas_resistance_ohm = 0.0f;

    return true;
}
