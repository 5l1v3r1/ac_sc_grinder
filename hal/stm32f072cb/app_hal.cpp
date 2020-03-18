#include "app_hal.h"

#include "main.h"
#include "adc.h"
#include "dma.h"
#include "spi.h"
#include "gpio.h"


extern "C" void SystemClock_Config(void);


namespace hal {

void setup(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_DMA_Init();
    MX_ADC_Init();
    MX_SPI2_Init();

    triac_ignition_off();
}

void triac_ignition_on() {
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7 | GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_SET);
}
void triac_ignition_off() {
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_7 | GPIO_PIN_6 | GPIO_PIN_7, GPIO_PIN_RESET);
}

}
