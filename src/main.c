#include <stdio.h>
#include "board_pins.h"
#include "setup.h"
#include "config.h"
#include "buttons.h"
#include "status_led.h"
#include "acquisition.h"
#include "grove_rgb.h"
#include "sd_logger.h"

#define CONFIG_IDLE_MS       (30UL * 60UL * 1000UL)
#define MAINTENANCE_PERIOD_MS 2000U

typedef enum { MODE_STANDARD, MODE_CONFIG, MODE_MAINTENANCE, MODE_ECO } Mode;

static const struct { uint8_t r, g, b; const char *name; } mode_info[] = {
    [MODE_STANDARD]    = {0, 255, 0, "standard"},
    [MODE_CONFIG]      = {255, 200, 0, "configuration"},
    [MODE_MAINTENANCE] = {255, 60, 0, "maintenance"},
    [MODE_ECO]         = {0, 0, 255, "economique"},
};
static Mode mode;
static Mode mode_before_maintenance;
static uint32_t last_measure; /* heure de la dernière mesure */
static bool measure_now; /* si vrai on mesure au prochain tour de boucle */
static uint32_t eco_count;

static void enter_mode(Mode m)
{
    if (m == MODE_MAINTENANCE)
    {
        mode_before_maintenance = mode;
        SDLogger_Unmount();/* on n'écrit plus sur la SD*/
    }
    else if (mode == MODE_MAINTENANCE)
        SDLogger_Init(); /* quand on sort si on a changé la SD on la recharge */

    mode = m;
    measure_now = true;
    StatusLed_SetMode(mode_info[m].r, mode_info[m].g, mode_info[m].b);
    printf("\r\nMode %s \r\n", mode_info[m].name);

    if (m == MODE_CONFIG)
        printf("Commandes : SHOW, PARAMETRE=VALEUR, RESET, VERSION, CLOCK=HH:MM:SS, DATE=MM,JJ,AAAA, DAY=MON\r\n> ");
}

static uint32_t measure_period_ms(void)
{
    if (mode == MODE_MAINTENANCE)
        return MAINTENANCE_PERIOD_MS;

    uint32_t ms = g_config.log_interval * 60000UL;
    return mode == MODE_ECO ? 2U * ms : ms;
}

static LedError pick_error(const AcqStatus *st, SDLogStatus sd)
{
    if (st->rtc_error) return LED_ERR_RTC;
    if (st->gps_error) return LED_ERR_GPS;
    if (sd == SDLOG_ERR_FULL) return LED_ERR_SD_FULL;
    if (sd == SDLOG_ERR_IO) return LED_ERR_SD_IO;
    if (st->sensor_error) return LED_ERR_SENSOR;
    if (st->data_error) return LED_ERR_DATA;
    return LED_ERR_NONE;
}

static void do_measure(void)
{
    char line[160];
    RTC_DateTime now;
    bool with_gps = mode != MODE_ECO || (eco_count++ % 2U) == 0U;

    AcqStatus st = Acquisition_Run(with_gps, line, sizeof(line), &now);
    printf("%s\r\n", line);

    SDLogStatus sd = SDLOG_OK;
    if (mode != MODE_MAINTENANCE)
    {
        if (!SDLogger_IsReady())
            SDLogger_Init();/* carte insérée après le démarrage */
        sd = SDLogger_WriteLine(&now, line, g_config.file_max_size);
        if (sd != SDLOG_OK)
            printf("Carte SD : %s\r\n", sd == SDLOG_ERR_FULL ? "pleine" : "erreur d'acces ou d'ecriture");
    }

    StatusLed_SetError(pick_error(&st, sd));
}
static void handle_console(uint32_t *last_activity)
{
    static char cmd[48];
    static unsigned int n = 0;
    int c;

    while ((c = Console_GetChar()) >= 0)
    {
        *last_activity = HAL_GetTick();

        if (c == '\r' || c == '\n')
        {
            if (n == 0) continue;
            cmd[n] = '\0';
            n = 0;
            printf("\r\n");
            Config_HandleCommand(cmd);
            printf("> ");
        }
        else if (n < sizeof(cmd) - 1)
        {
            cmd[n++] = (char)c;
            putchar(c);/*Pour voir ce que l'on écrit*/
            fflush(stdout);
        }
    }
}

int main(void)
{
    Global_Init();
    GroveRGB_Init();
    Buttons_Init();
    Config_Load();
    Acquisition_Init();

    printf("\r\nWWW version %s, lot %s\r\n", FW_VERSION, FW_LOT);
    if (!SDLogger_Init())
        printf("Carte SD absente ou probleme\r\n");

    mode = MODE_STANDARD;
    enter_mode(Buttons_IsPressed(BTN_RED) ? MODE_CONFIG : MODE_STANDARD);

    uint32_t last_activity = HAL_GetTick();

    while (1)
    {
        StatusLed_Update();
        ButtonEvent red = Buttons_GetEvent(BTN_RED);
        ButtonEvent white = Buttons_GetEvent(BTN_WHITE);

        switch (mode)
        {
        case MODE_CONFIG:
            handle_console(&last_activity);
            if (HAL_GetTick() - last_activity >= CONFIG_IDLE_MS)
                enter_mode(MODE_STANDARD);
            continue;/* acquisition désactivée en configuration */

        case MODE_STANDARD:
            if (red == BTN_EVT_LONG) enter_mode(MODE_MAINTENANCE);
            else if (white == BTN_EVT_LONG) enter_mode(MODE_ECO);
            break;

        case MODE_ECO:
            if (red == BTN_EVT_LONG) enter_mode(MODE_STANDARD);
            else if (white == BTN_EVT_LONG) enter_mode(MODE_MAINTENANCE);
            break;

        case MODE_MAINTENANCE:
            if (red == BTN_EVT_LONG) enter_mode(mode_before_maintenance);
            break;
        }

        while (Console_GetChar() >= 0) { }  /* ignorer écriture */

        if (measure_now || white == BTN_EVT_SHORT || HAL_GetTick() - last_measure >= measure_period_ms())
        {
            do_measure();
            measure_now = false;
            last_measure = HAL_GetTick();
        }
    }
}
