#include "status_led.h"
#include "board_pins.h"
#include "grove_rgb.h"

static uint8_t mode_r, mode_g, mode_b;/* couleur du mode en cours */
static LedError error = LED_ERR_NONE;
static uint32_t shown = 0xFFFFFFFF;/* rien au départ */

void StatusLed_SetMode(uint8_t r, uint8_t g, uint8_t b)
{
    mode_r = r;
    mode_g = g;
    mode_b = b;
    StatusLed_Update();
}

void StatusLed_SetError(LedError e)
{
    error = e;
    StatusLed_Update();
}

void StatusLed_Update(void)
{
    uint32_t color = ((uint32_t)mode_r << 16) | ((uint32_t)mode_g << 8) | mode_b;
    uint32_t other = 0;/* couleur alternée avec le rouge */
    uint32_t red_ms = 500;/* durée du rouge sur une période de 1000ms */

    switch (error)
    {
    case LED_ERR_RTC:     other = 0x0000FF; break;  /* rouge / bleu */
    case LED_ERR_GPS:     other = 0xFFA000; break;  /* rouge / jaune */
    case LED_ERR_SENSOR:  other = 0x00FF00; break;  /* rouge / vert */
    case LED_ERR_DATA:    other = 0x00FF00; red_ms = 333; break;  /* vert 2 fois plus long */
    case LED_ERR_SD_FULL: other = 0xFFFFFF; break;  /* rouge / blanc */
    case LED_ERR_SD_IO:   other = 0xFFFFFF; red_ms = 333; break;  /* blanc 2 fois plus long */
    default:              break;
    }

    if (error != LED_ERR_NONE)
        color = (HAL_GetTick() % 1000 < red_ms) ? 0xFF0000 : other;

    if (color != shown)
    {
        GroveRGB_SetColor(color >> 16, (color >> 8) & 0xFF, color & 0xFF);
        shown = color;
    }
}
