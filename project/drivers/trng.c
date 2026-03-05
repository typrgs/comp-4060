#include "trng.h"

void trngInit()
{
  MCLK_REGS->MCLK_APBCMASK |= MCLK_APBCMASK_TRNG_Msk;
  TRNG_REGS->TRNG_CTRLA = TRNG_CTRLA_ENABLE_Msk | TRNG_CTRLA_RUNSTDBY_Msk;
}

uint32_t trngRandom(uint32_t max)
{
  // wait for new data
  while ((TRNG_REGS->TRNG_INTFLAG & TRNG_INTFLAG_DATARDY_Msk) == 0)
    ;

  return (max > 0) ? (TRNG_REGS->TRNG_DATA % max) : TRNG_REGS->TRNG_DATA;
}