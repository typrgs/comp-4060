#include "icm.h"

static icm_descriptor_registers_t transferDesc __ALIGNED(64);
static uint8_t digest[32] __ALIGNED(128); // hash area is 32 bytes to hold a SHA256 digest
static uint8_t hashData[64]; // data to hash needs to be padded so it is 512 bits long

void icmInit()
{
  // enable main clock
  MCLK_REGS->MCLK_AHBMASK |= MCLK_AHBMASK_ICM_Msk;
  MCLK_REGS->MCLK_APBCMASK |= MCLK_APBCMASK_ICM_Msk;

  // set algorithm to SHA256
  ICM_REGS->ICM_CFG = ICM_CFG_UALGO_SHA256;

  // set start address of transfer descriptor
  ICM_REGS->ICM_DSCR = (uint32_t)&transferDesc;

  // set start address of hash area
  ICM_REGS->ICM_HASH = (uint32_t)digest;

  // set algorithm and EOM bit in transfer descriptor region
  transferDesc.ICM_RCFG = ICM_RCFG_ALGO_Msk | ICM_RCFG_PROCDLY_SHORT | ICM_RCFG_EOM_Msk;

  // disable region monitoring for all regions and enable ICM
  ICM_REGS->ICM_CTRL = ICM_CTRL_RMDIS(0) | ICM_CTRL_ENABLE_Msk;
}

static void padMsg(uint64_t msg)
{
  int hashDataPos = 0;

  // reset hash data region
  for(hashDataPos=0; hashDataPos<64; hashDataPos++)
  {
    hashData[hashDataPos] = 0;
  }

  // put the message into the hash data region
  for(hashDataPos=0; hashDataPos<4; hashDataPos++)
  {
    hashData[hashDataPos] = ((msg >> (64 - ((hashDataPos+1) * 16))) & 0xFF);
  }

  // append 1
  hashData[hashDataPos++] = 0x80;

  // use last 64 bits (8 bytes) to store the message length
  // (always set to 16, as a 64-bit integer can be represented as a hex string of length 16)
  hashData[59] = 0x01;
}

void SHA256(uint64_t msg, uint8_t *digest)
{
  // pad message and place in hash data area
  padMsg(msg);

  // compute new hash for given message
  ICM_REGS->ICM_CTRL = ICM_CTRL_REHASH(1);
  while((ICM_REGS->ICM_SR & ICM_ISR_RHC_Msk) == 0);
}
