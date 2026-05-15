#include "includes.h"
#include "GPIO_Init.h"
#include "timer.h"

#if defined(KNOB_RGB_COLOR)

// total 2.5us, run in 400Khz
#define NEOPIXEL_T0H_US 0.35  // Neopixel code 0 high level hold time in us
#define NEOPIXEL_T1H_US 2.15  // Neopixel code 1 high level hold time in us
uint16_t cycle, code_0_tim_h_cnt, code_1_tim_h_cnt;
uint32_t frameTimeStamp = 0xFFFFFFFF;  // Frame unit needs >280us for WS2812.

#ifdef GD32F2XX
  #define NEOPIXEL_TIMER_CLOCK_ENABLE() rcu_periph_clock_enable(RCU_TIMER5)
  #define NEOPIXEL_TIMER_CNT() TIMER_CNT(TIMER5)
  #define NEOPIXEL_TIMER_PSC() TIMER_PSC(TIMER5)
  #define NEOPIXEL_TIMER_ARR() TIMER_CAR(TIMER5)
  #define NEOPIXEL_TIMER_SR()  TIMER_INTF(TIMER5)
  #define NEOPIXEL_TIMER_CR1() TIMER_CTL0(TIMER5)
#else
  #define NEOPIXEL_TIMER_CLOCK_ENABLE() RCC->APB1ENR |= 1 << 4
  #define NEOPIXEL_TIMER_CNT() TIM6->CNT
  #define NEOPIXEL_TIMER_PSC() TIM6->PSC
  #define NEOPIXEL_TIMER_ARR() TIM6->ARR
  #define NEOPIXEL_TIMER_SR()  TIM6->SR
  #define NEOPIXEL_TIMER_CR1() TIM6->CR1
#endif

#endif // KNOB_RGB_COLOR

void KnobLed_Init(uint32_t PCLK1_Frequency) {
  #if defined(KNOB_RGB_COLOR)
    // Init hardware pin
    GPIO_InitSet(KNOB_LED_COLOR_PIN, MGPIO_MODE_OUT_PP, 0);
    GPIO_SetLevel(KNOB_LED_COLOR_PIN, 0);

    // Init timer
    NEOPIXEL_TIMER_CLOCK_ENABLE();
    NEOPIXEL_TIMER_CNT() = 0;
    NEOPIXEL_TIMER_PSC() = 1 - 1;
    NEOPIXEL_TIMER_SR() = (uint16_t)~(1 << 0);

    // Calculate timings
    cycle = PCLK1_Frequency * (0.000001 * (NEOPIXEL_T0H_US + NEOPIXEL_T1H_US)) / 2 - 1;  // Neopixel frequency
    code_0_tim_h_cnt = cycle * (NEOPIXEL_T0H_US / (NEOPIXEL_T0H_US + NEOPIXEL_T1H_US));  // Code 0, High level hold time,
    code_1_tim_h_cnt = cycle - code_0_tim_h_cnt;

    // Turn on knob LED
    KnobLed_On();
  #endif
}

void KnobLed_Set(uint32_t color) {
  #if defined(KNOB_RGB_COLOR)
    while (frameTimeStamp == Timer_GetTimerMs());

    // Disable interrupt, avoid disturbing the timing of WS2812
    __disable_irq();

    // Prepare timer
    NEOPIXEL_TIMER_ARR() = cycle;
    NEOPIXEL_TIMER_CR1() |= 0x01;

    // Loop over all LEDs
    for (uint8_t led_num = 0; led_num < NEOPIXEL_PIXELS; ++led_num) {
        // Loop over all bits
        for (uint8_t bit = 0; bit < 24; ++bit, color <<= 1) {
            // Clear timer counter
            NEOPIXEL_TIMER_CNT() = 0;

            // Set high level
            WS2812_FAST_WRITE_HIGH();

            // Check if bit is set and wait accordingly
            if (color & (1 << 23)) {
                while (NEOPIXEL_TIMER_CNT() < code_1_tim_h_cnt);
            } else {
                while (NEOPIXEL_TIMER_CNT() < code_0_tim_h_cnt);
            }

            // Set low level
            WS2812_FAST_WRITE_LOW();
            
            // Wait for time to elapse
            while (!NEOPIXEL_TIMER_SR());
            NEOPIXEL_TIMER_SR() = 0;
        }
    }

    // Stop timer
    NEOPIXEL_TIMER_CR1() &= ~0x01;

    // Enable interrupt
    __enable_irq();

    frameTimeStamp = Timer_GetTimerMs();
  #endif
}

void KnobLed_On() {
  #if defined(KNOB_RGB_COLOR)
    KnobLed_Set(KNOB_RGB_COLOR);
  #endif
}

void KnobLed_Off() {
  #if defined(KNOB_RGB_COLOR)
    KnobLed_Set(0x000000);
  #endif
}
