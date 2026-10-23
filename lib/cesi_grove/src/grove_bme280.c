#include "grove_bme280.h"
#include "board_pins.h"

extern I2C_HandleTypeDef GROVE_I2C_HANDLE;

/* Registres du BME280 (datasheet Bosch) */
#define BME280_REG_CALIB_TP  0x88    /* calibration : dig_T1..dig_T3 puis dig_P1..dig_P9 (24 octets) */
#define BME280_REG_CALIB_H1  0xA1    /* calibration humidité : dig_H1 (1 octet) */
#define BME280_REG_ID        0xD0    /* identifiant de la puce */
#define BME280_REG_RESET     0xE0    /* on y écrit 0xB6 pour réinitialiser la puce */
#define BME280_REG_CALIB_H2  0xE1    /* calibration humidité : dig_H2..dig_H6 (7 octets) */
#define BME280_REG_CTRL_HUM  0xF2    /* suréchantillonnage humidité (à écrire AVANT ctrl_meas) */
#define BME280_REG_STATUS    0xF3    /* bit 3 = mesure en cours */
#define BME280_REG_CTRL_MEAS 0xF4    /* suréchantillonnage température/pression + mode */
#define BME280_REG_DATA      0xF7    /* mesures brutes : pression (3 octets), température (3), humidité (2) */

#define BME280_CHIP_ID       0x60    /* un BMP280 répond 0x58 : on le refuse */
#define BME280_RESET_CMD     0xB6
#define BME280_MEASURING     0x08    /* bit 3 de status */
#define BME280_MODE_FORCED   0x01    /* bits 1:0 de ctrl_meas : une mesure, puis veille */

static uint16_t bme280_addr = (0x76 << 1);

/* Coefficients de calibration : propres à chaque puce, lus à l'initialisation */
static uint16_t dig_T1;
static int16_t  dig_T2, dig_T3;

static uint16_t dig_P1;
static int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;

static uint8_t  dig_H1, dig_H3;
static int16_t  dig_H2, dig_H4, dig_H5;
static int8_t   dig_H6;

/* Résultat intermédiaire du calcul de température, réutilisé pour la pression et l'humidité */
static float t_fine;

static bool read_reg(uint8_t reg, uint8_t *data, uint16_t len)
{
    if (HAL_I2C_Master_Transmit(&GROVE_I2C_HANDLE, bme280_addr, &reg, 1, 100) != HAL_OK)
        return false;

    return HAL_I2C_Master_Receive(&GROVE_I2C_HANDLE, bme280_addr, data, len, 100) == HAL_OK;
}

static bool write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    return HAL_I2C_Master_Transmit(&GROVE_I2C_HANDLE, bme280_addr, data, 2, 100) == HAL_OK;
}

/* Nombre de 16 bits rangé poids faible d'abord */
static uint16_t u16_le(const uint8_t *p)
{
    return (uint16_t)((p[1] << 8) | p[0]);
}

static bool read_calibration(void)
{
    uint8_t tp[24];     /* 0x88..0x9F */
    uint8_t h1;         /* 0xA1 */
    uint8_t h[7];       /* 0xE1..0xE7 */

    if (!read_reg(BME280_REG_CALIB_TP, tp, sizeof(tp))) return false;
    if (!read_reg(BME280_REG_CALIB_H1, &h1, 1))         return false;
    if (!read_reg(BME280_REG_CALIB_H2, h, sizeof(h)))   return false;

    dig_T1 = u16_le(&tp[0]);
    dig_T2 = (int16_t)u16_le(&tp[2]);
    dig_T3 = (int16_t)u16_le(&tp[4]);

    dig_P1 = u16_le(&tp[6]);
    dig_P2 = (int16_t)u16_le(&tp[8]);
    dig_P3 = (int16_t)u16_le(&tp[10]);
    dig_P4 = (int16_t)u16_le(&tp[12]);
    dig_P5 = (int16_t)u16_le(&tp[14]);
    dig_P6 = (int16_t)u16_le(&tp[16]);
    dig_P7 = (int16_t)u16_le(&tp[18]);
    dig_P8 = (int16_t)u16_le(&tp[20]);
    dig_P9 = (int16_t)u16_le(&tp[22]);

    dig_H1 = h1;
    dig_H2 = (int16_t)u16_le(&h[0]);
    dig_H3 = h[2];
    /* dig_H4 et dig_H5 font 12 bits chacun et se partagent l'octet 0xE5 (h[4]) */
    dig_H4 = (int16_t)(((int16_t)(int8_t)h[3] * 16) | (h[4] & 0x0F));
    dig_H5 = (int16_t)(((int16_t)(int8_t)h[5] * 16) | (h[4] >> 4));
    dig_H6 = (int8_t)h[6];

    return true;
}

bool GroveBME280_Init(void)
{
    uint8_t id = 0;

    /* Adresse 0x76 (SDO à la masse) ou 0x77 (SDO au 3,3 V) */
    bme280_addr = (0x76 << 1);
    if (!(read_reg(BME280_REG_ID, &id, 1) && id == BME280_CHIP_ID))
    {
        bme280_addr = (0x77 << 1);
        if (!(read_reg(BME280_REG_ID, &id, 1) && id == BME280_CHIP_ID))
            return false;
    }

    /* On remet la puce à zéro (démarrage : environ 2 ms), puis on lit sa calibration */
    if (!write_reg(BME280_REG_RESET, BME280_RESET_CMD))
        return false;
    HAL_Delay(10);

    return read_calibration();
}

/* Lance une mesure et lit les trois valeurs brutes. */
static bool read_raw(uint32_t *adc_P, uint32_t *adc_T, uint32_t *adc_H)
{
    /* Humidité x1, puis température x1, pression x1, mode "mesure unique".
       ctrl_hum n'est pris en compte qu'après une écriture de ctrl_meas : l'ordre compte. */
    if (!write_reg(BME280_REG_CTRL_HUM, 0x01)) return false;
    if (!write_reg(BME280_REG_CTRL_MEAS, (1U << 5) | (1U << 2) | BME280_MODE_FORCED)) return false;

    /* Le bit "mesure en cours" ne passe à 1 qu'après un court instant : on attend avant de le lire */
    HAL_Delay(5);

    uint8_t status = BME280_MEASURING;
    uint32_t debut = HAL_GetTick();
    while (status & BME280_MEASURING)
    {
        if (!read_reg(BME280_REG_STATUS, &status, 1)) return false;
        if ((HAL_GetTick() - debut) > 300) return false;      /* délai dépassé */
    }

    /* Une seule lecture des 8 octets : la puce garantit alors qu'ils viennent de la même mesure */
    uint8_t d[8];
    if (!read_reg(BME280_REG_DATA, d, sizeof(d))) return false;

    /* Pression et température : 20 bits (msb, lsb, 4 bits de poids fort de xlsb). Humidité : 16 bits. */
    *adc_P = ((uint32_t)d[0] << 12) | ((uint32_t)d[1] << 4) | ((uint32_t)d[2] >> 4);
    *adc_T = ((uint32_t)d[3] << 12) | ((uint32_t)d[4] << 4) | ((uint32_t)d[5] >> 4);
    *adc_H = ((uint32_t)d[6] << 8) | d[7];
    return true;
}

/*
 * Les trois conversions suivent les formules flottantes du pilote officiel Bosch
 * (github.com/boschsensortec/BME280_SensorAPI). Les nombres 16384, 524288... sont des
 * constantes de ces formules ; les dig_xx sont les coefficients propres à la puce.
 * En C, on divise toujours en float : une division entière tronquerait le résultat.
 */

/* Température en °C. Calcule aussi t_fine : à appeler AVANT la pression et l'humidité. */
static float compensate_temperature(uint32_t adc)
{
    float var1 = ((float)adc / 16384.0f - (float)dig_T1 / 1024.0f) * (float)dig_T2;
    float a    = (float)adc / 131072.0f - (float)dig_T1 / 8192.0f;
    float var2 = a * a * (float)dig_T3;
    t_fine = var1 + var2;

    return t_fine / 5120.0f;
}

/* Pression en hPa */
static float compensate_pressure(uint32_t adc)
{
    float var1 = t_fine / 2.0f - 64000.0f;
    float var2 = var1 * var1 * (float)dig_P6 / 32768.0f;
    var2 = var2 + var1 * (float)dig_P5 * 2.0f;
    var2 = var2 / 4.0f + (float)dig_P4 * 65536.0f;
    float var3 = (float)dig_P3 * var1 * var1 / 524288.0f;
    var1 = (var3 + (float)dig_P2 * var1) / 524288.0f;
    var1 = (1.0f + var1 / 32768.0f) * (float)dig_P1;

    if (var1 <= 0.0f)
        return 0.0f;                        /* calibration invalide : évite une division par zéro */

    float p = 1048576.0f - (float)adc;
    p = (p - var2 / 4096.0f) * 6250.0f / var1;
    var1 = (float)dig_P9 * p * p / 2147483648.0f;
    var2 = p * (float)dig_P8 / 32768.0f;
    p = p + (var1 + var2 + (float)dig_P7) / 16.0f;      /* en pascals */

    return p / 100.0f;
}

/* Humidité relative en % */
static float compensate_humidity(uint32_t adc)
{
    float var1 = t_fine - 76800.0f;
    float var2 = (float)dig_H4 * 64.0f + ((float)dig_H5 / 16384.0f) * var1;
    float var3 = (float)adc - var2;
    float var4 = (float)dig_H2 / 65536.0f;
    float var5 = 1.0f + ((float)dig_H3 / 67108864.0f) * var1;
    float var6 = 1.0f + ((float)dig_H6 / 67108864.0f) * var1 * var5;
    var6 = var3 * var4 * (var5 * var6);
    float h = var6 * (1.0f - (float)dig_H1 * var6 / 524288.0f);

    /* Près des extrêmes, la formule peut sortir un peu de 0..100 % */
    if (h > 100.0f) h = 100.0f;
    if (h < 0.0f)   h = 0.0f;
    return h;
}

bool GroveBME280_ReadEnvironment(Env_Data *data)
{
    if (data == 0) return false;

    uint32_t adc_P = 0, adc_T = 0, adc_H = 0;
    if (!read_raw(&adc_P, &adc_T, &adc_H))
        return false;

    /* L'ordre compte : la température calcule t_fine, utilisé ensuite par les deux autres */
    data->temperature_c    = compensate_temperature(adc_T);
    data->pressure_hpa     = compensate_pressure(adc_P);
    data->humidity_percent = compensate_humidity(adc_H);
    data->gas_resistance_ohm = 0.0f;        /* le BME280 n'a pas de capteur de gaz */

    return true;
}
