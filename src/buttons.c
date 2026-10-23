#include "buttons.h"
#include "board_pins.h"

#define DEBOUNCE_MS 30U

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;

    volatile uint32_t last_edge;

    bool stable_pressed;
    uint32_t pressed_since;
    bool long_sent;
} Button;

static Button buttons[2] = {
    {BUTTON_RED_PORT, BUTTON_RED_PIN, 0, false, 0, false},
    {BUTTON_WHITE_PORT, BUTTON_WHITE_PIN, 0, false, 0, false},
};

static bool read_pressed(const Button *b)
{
    return HAL_GPIO_ReadPin(b->port, b->pin) == BUTTON_PRESSED_LEVEL;
}

void Buttons_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_SYSCFG_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
    gpio.Pull = GPIO_PULLUP;

    for (int i = 0; i < 2; i++)
    {
        gpio.Pin = buttons[i].pin;
        HAL_GPIO_Init(buttons[i].port, &gpio);
        buttons[i].stable_pressed = read_pressed(&buttons[i]);
        buttons[i].long_sent = buttons[i].stable_pressed;
    }

    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
}

bool Buttons_IsPressed(ButtonId id)
{
    return read_pressed(&buttons[id]);
}

void EXTI9_5_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(BUTTON_RED_PIN);
    HAL_GPIO_EXTI_IRQHandler(BUTTON_WHITE_PIN);
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    for (int i = 0; i < 2; i++)
        if (GPIO_Pin == buttons[i].pin)
            buttons[i].last_edge = HAL_GetTick();
}

ButtonEvent Buttons_GetEvent(ButtonId id)
{
    Button *b = &buttons[id];
    uint32_t now = HAL_GetTick();

    if (now - b->last_edge >= DEBOUNCE_MS)
    {
        bool pressed = read_pressed(b);

        if (pressed && !b->stable_pressed)
        {
            b->stable_pressed = true;
            b->pressed_since = b->last_edge;
            b->long_sent = false;
        }
        else if (!pressed && b->stable_pressed)
        {
            b->stable_pressed = false;
            if (!b->long_sent)
                return BTN_EVT_SHORT;
        }
    }

    if (b->stable_pressed && !b->long_sent && now - b->pressed_since >= BUTTON_LONG_PRESS_MS)
    {
        b->long_sent = true;
        return BTN_EVT_LONG;
    }

    return BTN_EVT_NONE;
}
