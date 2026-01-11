#include "common.h"
#include "heart.h"

// setup our heartbeat to be 1ms: so we overflow at 1ms intervals with a 48MHz clock
#define MS_TICKS 48000UL

// NOTE: this overflows every ~50 days, so I'm not going to care here...
volatile uint32_t tickCount = 0;

uint32_t elapsedMS()
{
  return tickCount;
}

void heartInit()
{
  NVIC_EnableIRQ(SysTick_IRQn);

  SysTick_Config(MS_TICKS);
}

// Fires every 1ms or .1ms if overclocked
void SysTick_Handler()
{
  tickCount++;
}
