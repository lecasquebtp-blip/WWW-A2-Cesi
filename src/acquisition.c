#include "acquisition.h"
#include "config.h"
#include "board_pins.h"
#include "grove_bme280.h"
#include "grove_light.h"
#include "grove_gps_air530z.h"
#include "grove_rtc_ds1307.h"

#include <stdio.h>
#include <string.h>

#define MAX_ERRORS 2 /* Après 2 mesure, on signale une erreur */

/* Nombre de mesures ratées à la suite */
static int bme_errors = 0;
static int light_errors = 0;
static int gps_errors = 0;

void Acquisition_Init(void)
{
    GroveRTC_Init();
    GroveLight_Init();
    GroveGPS_Init();
    GroveBME280_Init();
}

static uint32_t wait_time_ms(int errors)
{
    if (errors >= MAX_ERRORS)
        return 0;
    return g_config.timeout * 1000;
}

static bool read_bme(Env_Data *env)
{
    uint32_t start = HAL_GetTick();

    do
    {
        if (GroveBME280_Init() && GroveBME280_ReadEnvironment(env))
            return true;
        HAL_Delay(200);
    } while (HAL_GetTick() - start < wait_time_ms(bme_errors));

    return false;
}


static void to_text(char *text, const char *format, float value, bool valid)
{
    if (valid)
        sprintf(text, format, value);
    else
        strcpy(text, "NA"); /* Si pas de valeur */
}

AcqStatus Acquisition_Run(bool with_gps, char *line, size_t len, RTC_DateTime *now)
{
    AcqStatus status = {0};
    char temp[12], hum[12], press[12], light[12], lat[16], lon[16];
    const char *light_class;

    if (!GroveRTC_GetDateTime(now))
    {
        status.rtc_error = true;
        memset(now, 0, sizeof(*now));
    }
    Env_Data env = {0};
    bool bme_ok = false;

    if (g_config.temp_air || g_config.hygr || g_config.pressure)
    {
        bme_ok = read_bme(&env);
        bme_errors = bme_ok ? 0 : bme_errors + 1;
    }

    to_text(temp, "%.2f", env.temperature_c, bme_ok);
    to_text(press, "%.1f", env.pressure_hpa, bme_ok);
    bool hum_ok = bme_ok && env.temperature_c >= g_config.hygr_mint && env.temperature_c <= g_config.hygr_maxt;
    to_text(hum, "%.1f", env.humidity_percent, hum_ok);

    if (bme_ok && g_config.temp_air
        && (env.temperature_c < g_config.min_temp_air || env.temperature_c > g_config.max_temp_air))
        status.data_error = true;

    if (bme_ok && g_config.pressure
        && (env.pressure_hpa < g_config.pressure_min || env.pressure_hpa > g_config.pressure_max))
        status.data_error = true;

    if (!g_config.temp_air) strcpy(temp, "OFF");
    if (!g_config.hygr)     strcpy(hum, "OFF");
    if (!g_config.pressure) strcpy(press, "OFF");
    if (!g_config.lumin)
    {
        strcpy(light, "OFF");
        light_class = "OFF";
    }
    else
    {
        uint16_t value = GroveLight_ReadRaw();
        bool light_ok = value != 0xFFFF;
        light_errors = light_ok ? 0 : light_errors + 1;
        to_text(light, "%.0f", value, light_ok);
        if (!light_ok)                       light_class = "NA";
        else if (value < g_config.lumin_low)  light_class = "faible";
        else if (value > g_config.lumin_high) light_class = "forte";
        else                                  light_class = "moyenne";
    }
    GPS_Data gps = {0};
    bool gps_answered = false;

    if (with_gps)
    {
        uint32_t wait = wait_time_ms(gps_errors);
        if (wait == 0)
            wait = 1500; /* Une trame par sec */
        gps_answered = GroveGPS_ReadGGA(&gps, wait);
        gps_errors = gps_answered ? 0 : gps_errors + 1;
    }

    to_text(lat, "%.5f", gps.latitude, gps_answered && gps.valid);
    to_text(lon, "%.5f", gps.longitude, gps_answered && gps.valid);
    snprintf(line, len, "%04u-%02u-%02u %02u:%02u:%02u;%s;%s;%s;%s;%s;%s;%s",
             now->year, now->month, now->day, now->hours, now->minutes, now->seconds,
             temp, hum, press, light, light_class, lat, lon);

    status.sensor_error = bme_errors >= MAX_ERRORS || light_errors >= MAX_ERRORS;
    status.gps_error = gps_errors >= MAX_ERRORS;
    return status;
}
