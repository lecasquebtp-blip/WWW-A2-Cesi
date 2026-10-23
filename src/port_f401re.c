/*
 * Version NUCLEO-F401RE, la carte réellement utilisée par le groupe.
 *
 * Le dépôt d'origine visait une NUCLEO-L476RG : setup.c et grove_light.c utilisent
 * du code propre au L4 (horloge MSI, ADC). Ils restent dans le projet mais ne sont
 * compilés que pour STM32L4. Ce fichier fournit les mêmes fonctions pour le F401RE.
 * Le GPS utilise la bibliothèque fournie (grove_gps_air530z.c) sur l'USART1.
 */
#ifdef STM32F4

#include "setup.h"
#include "board_pins.h"
#include "grove_light.h"

I2C_HandleTypeDef hi2c1;
SPI_HandleTypeDef hspi1;
UART_HandleTypeDef huart2;
ADC_HandleTypeDef hadc1;

/* printf -> moniteur série (USART2, relié au ST-Link) */
int _write(int file, char *ptr, int len)
{
    (void)file;
    HAL_UART_Transmit(&huart2, (uint8_t *)ptr, len, HAL_MAX_DELAY);
    return len;
}

int Console_GetChar(void)
{
    if (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_ORE))
        __HAL_UART_CLEAR_OREFLAG(&huart2);  /* caractères perdus pendant une mesure : on repart proprement */

    if (!__HAL_UART_GET_FLAG(&huart2, UART_FLAG_RXNE))
        return -1;
    return (int)(huart2.Instance->DR & 0xFFU);
}

/* Timer interne : fait avancer le compteur de HAL_Delay() */
void SysTick_Handler(void)
{
    HAL_IncTick();
}

/* Si la carte a redémarré au milieu d'un échange, un module peut garder SDA à 0 et bloquer
   tout le bus. 9 coups d'horloge sur SCL lui font finir son octet, puis on envoie un STOP. */
static void i2c1_bus_recovery(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;     // PB8 SCL, PB9 SDA
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8 | GPIO_PIN_9, GPIO_PIN_SET);
    HAL_GPIO_Init(GPIOB, &gpio);

    for (int i = 0; i < 9 && HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9) == GPIO_PIN_RESET; i++)
    {
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET);
        HAL_Delay(1);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET);
        HAL_Delay(1);
    }

    /* STOP : SDA passe de 0 à 1 pendant que SCL est à 1 */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET);
    HAL_Delay(1);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET);
    HAL_Delay(1);
}

static void MX_I2C1_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();
    i2c1_bus_recovery();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;     // PB8 SCL, PB9 SDA
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &gpio);

    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;         // 100 kHz : le F4 n'a pas de champ Timing
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&hi2c1);
}

static void MX_SPI1_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_7;     // PA5 SCK, PA7 MOSI
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_6;                  // PA6 MISO, avec pull-up comme sur la L476RG
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);

    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Pin = SD_CS_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = 0;
    HAL_GPIO_Init(SD_CS_PORT, &gpio);
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET);

    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 7;
    HAL_SPI_Init(&hspi1);
}

void HAL_UART_MspInit(UART_HandleTypeDef *uartHandle)
{
    if (uartHandle->Instance == USART2)
    {
        /* PA2=TX, PA3=RX, reliés au ST-Link */
        __HAL_RCC_USART2_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        GPIO_InitTypeDef gpio = {0};
        gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3;
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Pull = GPIO_NOPULL;
        gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        gpio.Alternate = GPIO_AF7_USART2;
        HAL_GPIO_Init(GPIOA, &gpio);
    }
    else if (uartHandle->Instance == USART1)
    {
        /* GPS : le TX du module arrive sur PA10 = USART1_RX (port Grove D2).
           On ne branche que la réception : PA9 (TX) sert au bouton rouge. */
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        GPIO_InitTypeDef gpio = {0};
        gpio.Pin = GPS_RX_PIN;
        gpio.Mode = GPIO_MODE_AF_PP;
        gpio.Pull = GPIO_PULLUP;
        gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
        gpio.Alternate = GPIO_AF7_USART1;
        HAL_GPIO_Init(GPS_RX_PORT, &gpio);
    }
}

static void MX_USART2_UART_Init(void)
{
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart2) != HAL_OK)
    {
        while (1);
    }
}

void Global_Init(void)
{
    HAL_Init();                             // horloge : on garde le HSI 16 MHz du démarrage

    MX_I2C1_Init();
    MX_SPI1_Init();

    MX_USART2_UART_Init();
}

/* ---- Capteur de lumière : PA4 = canal 4 de l'ADC sur le F401 ---- */

void GroveLight_Init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_ADC1_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = LIGHT_GPIO_PIN;
    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(LIGHT_GPIO_PORT, &gpio);

    hadc1.Instance = ADC1;
    hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc1.Init.Resolution = ADC_RESOLUTION_10B;      // 0..1023, comme les seuils LUMIN_LOW/LUMIN_HIGH du sujet
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.ScanConvMode = DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    HAL_ADC_Init(&hadc1);

    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = LIGHT_ADC_CHANNEL;
    sConfig.Rank = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_56CYCLES;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
}

uint16_t GroveLight_ReadRaw(void)
{
    if (HAL_ADC_Start(&hadc1) != HAL_OK)
        return 0xFFFF;

    if (HAL_ADC_PollForConversion(&hadc1, 100) != HAL_OK)
    {
        HAL_ADC_Stop(&hadc1);
        return 0xFFFF;
    }

    uint16_t value = (uint16_t)HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);
    return value;
}

#endif /* STM32F4 */
