#include "timer.h"

volatile uint32_t ui32TickMs = 0;

void Timer_Init(RCC_ClocksTypeDef* rccClocks) {
  // Reset tick counter
  ui32TickMs = 0;

#if defined(GD32F2XX)
  // Setup timer7 interrupt
  nvic_irq_enable(TIMER6_IRQn, 2U, 0U);

  // Enable clock for timer
  rcu_periph_clock_enable(RCU_TIMER6);

  // Configure timer
  uint32_t timerTmpClk = rccClocks->PCLK1_Frequency;
  if (timerTmpClk < rccClocks->HCLK_Frequency) {
    timerTmpClk *= 2;
  }
  TIMER_CAR(TIMER6) = 1000 - 1;
  TIMER_PSC(TIMER6) = timerTmpClk / 1000000 - 1;
  TIMER_INTF(TIMER6) = (uint16_t)~TIMER_INTF_UPIF;
  TIMER_DMAINTEN(TIMER6) |= 1<<0;
  TIMER_CTL0(TIMER6) |= 0x01;
#else
  // Setup timer7 interrupt
  NVIC_InitTypeDef NVIC_InitStructure;
  NVIC_InitStructure.NVIC_IRQChannel = TIM7_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);
  
  // Enable clock for timer
  RCC->APB1ENR |= 1<<5;
  
  // Configure timer
  TIM7->ARR = 1000 - 1;
  if (rccClocks->PCLK1_Frequency < rccClocks->HCLK_Frequency) {
    TIM7->PSC = rccClocks->PCLK1_Frequency / 500000 - 1;
  } else {
    TIM7->PSC = rccClocks->PCLK1_Frequency / 1000000 - 1;  
  }
  TIM7->SR = (uint16_t)~(1<<0);
  TIM7->DIER |= 1<<0;
  TIM7->CR1 |= 0x01;
#endif
}

uint32_t Timer_GetTimerMs() {
  // Return current tick counter
  return ui32TickMs;
}

#if defined(GD32F2XX)
void TIMER6_IRQHandler(void) {
  // Check interrupt flag
  if ((TIMER_INTF(TIMER6) & TIMER_INTF_UPIF) != 0) {
    // Clear flag before running timer work, matching the GD boot fix.
    TIMER_INTF(TIMER6) &= ~TIMER_INTF_UPIF;

    // Increate tick counter
    ++ui32TickMs;
  }
}
#else
void TIM7_IRQHandler(void) {
  // Check interrupt flag
  if ((TIM7->SR & 0x01) != 0) {
    // Clear flag
    TIM7->SR &= (uint16_t)~(1<<0);
	
    // Increate tick counter
    ++ui32TickMs;
  }
}
#endif
