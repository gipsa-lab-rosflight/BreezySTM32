/*
   drv_sytem.c : system utilities (init, reset, delay, etc.)  for STM32F103CB

   Adapted from https://github.com/multiwii/baseflight/blob/master/src/drv_system.c

   This file is part of BreezySTM32.

   BreezySTM32 is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   BreezySTM32 is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with BreezySTM32.  If not, see <http://www.gnu.org/licenses/>.
 */


#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "stm32f10x_conf.h"

#include "drv_gpio.h"
#include "drv_system.h"

// cycles per microsecond
static volatile uint32_t usTicks = 0;
// current uptime for 1kHz systick timer. will rollover after 49 days. hopefully we won't care.
static volatile uint32_t sysTickUptime = 0;

static void cycleCounterInit(void)
{
    RCC_ClocksTypeDef clocks;
    RCC_GetClocksFreq(&clocks);
    usTicks = clocks.SYSCLK_Frequency / 1000000;
}

// SysTick
void SysTick_Handler(void)
{
    sysTickUptime++;
}

// Return system uptime in microseconds (rollover in 49 days because of ms rollover)
uint64_t micros(void)
{
    volatile uint32_t ms, cycle_cnt;
    do {
        ms = sysTickUptime;
        cycle_cnt = SysTick->VAL;
    } while (ms != sysTickUptime);
  return (uint64_t)ms * (uint64_t)1000 + (uint64_t)((usTicks * 1000 - cycle_cnt) / usTicks);
}

// Return system uptime in milliseconds (rollover in 49 days)
uint32_t millis(void)
{
    return sysTickUptime;
}

void systemInit(void)
{
    struct {
        GPIO_TypeDef *gpio;
        gpio_config_t cfg;
    } gpio_setup[3];

    gpio_setup[0].gpio = LED0_GPIO;
    gpio_setup[0].cfg.pin = LED0_PIN;
    gpio_setup[0].cfg.mode = Mode_Out_PP;
    gpio_setup[0].cfg.speed = Speed_2MHz;

    gpio_setup[1].gpio = LED1_GPIO;
    gpio_setup[1].cfg.pin = LED1_PIN;
    gpio_setup[1].cfg.mode = Mode_Out_PP;
    gpio_setup[1].cfg.speed = Speed_2MHz;

    gpio_setup[2].gpio = INV_GPIO;
    gpio_setup[2].cfg.pin = INV_PIN;
    gpio_setup[2].cfg.mode = Mode_Out_PP;
    gpio_setup[2].cfg.speed = Speed_2MHz;

    gpio_config_t gpio;
    int i, gpio_count = sizeof(gpio_setup) / sizeof(gpio_setup[0]);

    // Configure NVIC preempt/priority groups
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    // Turn on clocks for stuff we use
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2 | RCC_APB1Periph_TIM3 | RCC_APB1Periph_TIM4, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOC | RCC_APB2Periph_TIM1 | RCC_APB2Periph_ADC1 | RCC_APB2Periph_USART1, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    RCC_ClearFlag();

    // Make all GPIO in by default to save power and reduce noise
    gpio.pin = Pin_All;
    gpio.mode = Mode_AIN;
    gpioInit(GPIOA, &gpio);
    gpioInit(GPIOB, &gpio);
    gpioInit(GPIOC, &gpio);

    // Turn off JTAG port 'cause we're using the GPIO for leds
#define AFIO_MAPR_SWJ_CFG_NO_JTAG_SW            (0x2 << 24)
    AFIO->MAPR |= AFIO_MAPR_SWJ_CFG_NO_JTAG_SW;

    LED0_OFF;
    LED1_OFF;

    for (i = 0; i < gpio_count; i++) {
        gpioInit(gpio_setup[i].gpio, &gpio_setup[i].cfg);
    }

    // Init cycle counter
    cycleCounterInit();

    // SysTick
    SysTick_Config(SystemCoreClock / 1000);
}

void delayMicroseconds(uint32_t us)
{
    uint64_t now = micros();
    while(micros() < (now + us));
}

void delay(uint32_t ms)
{
    while (ms--)
        delayMicroseconds(1000);
}

void failureMode()
{
    LED1_OFF;
    LED0_ON;
    systemReset(false);
}

uint32_t rccReadBkpDr(void)
{
    return *((uint16_t *)BKP_BASE + 0x04) | *((uint16_t *)BKP_BASE + 0x08) << 16;
}

void rccWriteBkpDr(uint32_t value)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR | RCC_APB1Periph_BKP, ENABLE);
    PWR->CR |= PWR_CR_DBP;

    *((uint16_t *)BKP_BASE + 0x04) = value & 0xffff;
    *((uint16_t *)BKP_BASE + 0x08) = (value & 0xffff0000) >> 16;
}

#define AIRCR_VECTKEY_MASK    ((uint32_t)0x05FA0000)

void systemReset(bool toBootloader)
{
    if (toBootloader) {
        // 1FFFF000 -> 20000200 -> SP
        // 1FFFF004 -> 1FFFF021 -> PC
        *((uint32_t *)0x20004FF0) = 0xDEADBEEF; // 20KB STM32F103
    }

    // write magic value that we're doing a soft reset
    rccWriteBkpDr(BKP_SOFTRESET);

    // Generate system reset
    SCB->AIRCR = AIRCR_VECTKEY_MASK | (uint32_t)0x04;
}
