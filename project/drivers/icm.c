#include "icm.h"

static icm_descriptor_registers_t transferDesc __ALIGNED(64);
static uint32_t transferDescRegion[4]; // transfer descriptor region
static uint8_t hashArea[32] __ALIGNED(128);

void icmInit()
{
  // enable main clock
  MCLK_REGS->MCLK_AHBMASK |= MCLK_AHBMASK_ICM_Msk;
  MCLK_REGS->MCLK_APBCMASK |= MCLK_APBCMASK_ICM_Msk;

  // set algorithm to SHA256
  ICM_REGS->ICM_CFG = ICM_CFG_UALGO_SHA256;

  // set start address of transfer descriptor
  ICM_REGS->ICM_DSCR = (uint32_t)&transferDesc;

  // set algorithm and EOM bit in transfer descriptor region
  transferDesc.ICM_RCFG = ICM_RCFG_ALGO_Msk | ICM_RCFG_PROCDLY_SHORT | ICM_RCFG_EOM_Msk;

  // set region of transfer descriptor
  transferDesc.ICM_RADDR = (uint32_t)transferDescRegion;

  // enable ICM
  ICM_REGS->ICM_CTRL = ICM_CTRL_ENABLE_Msk;
}

static void padMsg(uint64_t msg)
{

}

void SHA256(uint64_t msg, uint8_t *digest)
{

}
