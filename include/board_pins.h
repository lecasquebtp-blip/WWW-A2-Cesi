#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#if defined(STM32F4)
#include "stm32f4xx_hal.h"      /* NUCLEO-F401RE : notre carte */
#else
#endif

/*
 * Grove Chainable RGB LED :
 * - Port Grove D6 du shield
 * - Signal1=D6=PB10 → CIN (horloge P9813)
 * - Signal2=D7=PA8  → DIN (données P9813)
 */
#define RGB_DATA_PORT   GPIOA
#define RGB_DATA_PIN    GPIO_PIN_8

#define RGB_CLK_PORT    GPIOB
#define RGB_CLK_PIN     GPIO_PIN_10

/*
 * Grove Light Sensor v1.3
 * Port Grove A2 du shield (PA4 = ADC1_IN9).
 */
#if defined(STM32F4)
#define LIGHT_ADC_CHANNEL  ADC_CHANNEL_4    /* PA4 = ADC1_IN4 sur le F401 */
#else
#define LIGHT_ADC_CHANNEL  ADC_CHANNEL_9
#endif
#define LIGHT_GPIO_PORT    GPIOA
#define LIGHT_GPIO_PIN     GPIO_PIN_4

/*
 * Grove Dual Button v1.0
 * Port Grove D8 : PA9 (D8) = bouton 1, PC7 (D9) = bouton 2.
 * (Le port D2 est pris par le GPS : PA10 est la seule broche de réception UART libre.)
 */
#define BUTTON1_PORT GPIOA
#define BUTTON1_PIN  GPIO_PIN_9

#define BUTTON2_PORT GPIOC
#define BUTTON2_PIN  GPIO_PIN_7

/* Rôles du sujet : bouton rouge (configuration, maintenance, retour) et bouton blanc (économique).
   Si les couleurs sont inversées sur le module, échanger 1 et 2 ici. */
#define BUTTON_RED_PORT     BUTTON1_PORT
#define BUTTON_RED_PIN      BUTTON1_PIN
#define BUTTON_WHITE_PORT   BUTTON2_PORT
#define BUTTON_WHITE_PIN    BUTTON2_PIN
#define BUTTON_PRESSED_LEVEL GPIO_PIN_RESET  /* le module tire la broche à la masse quand on appuie */

/*
 * Grove GPS Air530Z, bibliothèque fournie (grove_gps_air530z.c)
 * Port Grove D2 : le TX du module (fil jaune) arrive sur PA10 = USART1_RX.
 * (La L476RG utilisait l'UART4 sur PA0, qui n'existe pas sur la F401RE.)
 */
#define GPS_RX_PORT GPIOA
#define GPS_RX_PIN  GPIO_PIN_10

#if defined(STM32F4)
#define GPS_UART_INSTANCE USART1
#else
#define GPS_UART_INSTANCE UART4
#endif
#define GPS_UART_HANDLE huart_gps

/*
 * Grove RTC v1.2 DS1307 + BME280
 * Tous deux sur I2C.
 */
#define GROVE_I2C_HANDLE hi2c1

/*
 * SD Card Shield Seeed v4.3
 * SPI Arduino : D11/D12/D13 et CS sur D4.
 */
#define SD_CS_PORT GPIOB
#define SD_CS_PIN  GPIO_PIN_5

#define SD_SPI_HANDLE hspi1

#endif
