#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdbool.h>

typedef enum { BTN_RED = 0, BTN_WHITE = 1 } ButtonId;
typedef enum { BTN_EVT_NONE = 0, BTN_EVT_SHORT, BTN_EVT_LONG } ButtonEvent;

#define BUTTON_LONG_PRESS_MS 5000U

void Buttons_Init(void);
bool Buttons_IsPressed(ButtonId id);
ButtonEvent Buttons_GetEvent(ButtonId id);

#endif
