#include "heart.h"

#define SYSTICK_INIT_VAL 48000

volatile uint32_t msCount = 0;

void heartInit()
{
  SysTick_Config(SYSTICK_INIT_VAL);

  NVIC_EnableIRQ(SysTick_IRQn);
}

uint32_t elapsedMS()
{
  return msCount;
}

void SysTick_Handler()
{
  msCount++;
}
