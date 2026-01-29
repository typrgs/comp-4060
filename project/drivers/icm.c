#include "icm.h"

static uint32_t transferDesc[4];
static uint8_t hashArea[128];

void icmInit()
{
  // enable main clock
  MCLK_REGS->MCLK_AHBMASK |= MCLK_AHBMASK_ICM_Msk;
  MCLK_REGS->MCLK_APBCMASK |= MCLK_APBCMASK_ICM_Msk;

  // set algorithm to SHA256
  ICM_REGS->ICM_CFG = ICM_CFG_UALGO_SHA256;
}

static void padMsg(uint64_t msk)
{

}

void SHA256(uint64_t msg, uint8_t *digest)
{

}
