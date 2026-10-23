#ifndef STATUS_LED_H
#define STATUS_LED_H

#include <stdint.h>

typedef enum
{
    LED_ERR_NONE = 0,
    LED_ERR_RTC,/* rouge / bleu, 1 Hz, durées égales */
    LED_ERR_GPS,/* rouge / jaune, 1 Hz, durées égales */
    LED_ERR_SENSOR,/* rouge / vert, 1 Hz, durées égales */
    LED_ERR_DATA,/* rouge / vert, 1 Hz, vert 2 fois plus long : données incohérentes */
    LED_ERR_SD_FULL,/* rouge / blanc, 1 Hz, durées égales */
    LED_ERR_SD_IO,/* rouge / blanc, 1 Hz, blanc 2 fois plus long */
} LedError;

void StatusLed_SetMode(uint8_t r, uint8_t g, uint8_t b);/* couleur continue du mode */
void StatusLed_SetError(LedError error); /* une erreur remplace la couleur du mode */
void StatusLed_Update(void); /* à appeler souvent : fait clignoter */

#endif
