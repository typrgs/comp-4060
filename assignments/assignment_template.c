#include "sam.h"
#include "../dcc_stdio.h"
#include "../display.h"

#define MS_TICKS 48000UL

// counts the # of ms that have passed
volatile uint32_t ms_count = 0;


/************* INIT *************/

void systicks_init()
{
  NVIC_EnableIRQ(SysTick_IRQn);

  SysTick_Config(MS_TICKS);
}


/************* HANDLERS *************/

void SysTick_Handler()
{
  ms_count++;
}


/************* TASKS + HELPERS *************/

int main()
{
#ifndef NDEBUG
  for (int i=0; i<100000; i++)
  ;
  // include the following line if you want to simulate the 'standard' stop on entry behaviour
  // WARNING: this will always breakpoint, even when not in a debugger. Meaning that your code will never execute if outside a debugger.
  // __BKPT(0);
#endif

  // allow interrupts globally
  __enable_irq();

  // sleep to idle (wake on interrupts)
  PM_REGS->PM_SLEEPCFG = PM_SLEEPCFG_SLEEPMODE_IDLE;

  systicks_init();
  for(;;)
  {
    __WFI();
  }

  return 0;
}