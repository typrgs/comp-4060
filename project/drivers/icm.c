#include "icm.h"

static icm_descriptor_registers_t transferDesc __ALIGNED(64) = {0};
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
  ICM_REGS->ICM_HASH = (uint32_t)(&digest[0]);

  // set start address of data region
  transferDesc.ICM_RADDR = (uint32_t)(&hashData[0]);

  // set SHA256 algorithm, shortest processing delay, and set end of monitoring bit
  transferDesc.ICM_RCFG = ICM_RCFG_ALGO(1) | ICM_RCFG_PROCDLY_SHORT | ICM_RCFG_EOM_Msk;
}

static void padMsg(uint8_t *msg, uint64_t msgLen)
{
  uint8_t hashDataPos = 0;

  // reset hash data region
  for(hashDataPos=0; hashDataPos<64; hashDataPos++)
  {
    hashData[hashDataPos] = 0;
  }

  // put the message into the hash data region
  for(hashDataPos=0; hashDataPos<msgLen; hashDataPos++)
  {
    hashData[hashDataPos] = msg[hashDataPos];
  }

  // append 1
  hashData[hashDataPos++] = 0x80;

  // use last 64 bits (8 bytes) to store the message length, big endian
  // address the length variable in bytes
  uint8_t *lenPtr = (uint8_t *)&msgLen;

  // place msgLen bytes in big endian order
  for(int i=7; i>=0; i--)
  {
    hashData[63-i] = lenPtr[i];
  }
}

void icmSHA256(uint8_t *msg, uint64_t msgLen, uint8_t *result)
{
  // pad message and place in hash data area
  padMsg(msg, msgLen);
  
  // enable ICM
  ICM_REGS->ICM_CTRL = ICM_CTRL_ENABLE_Msk;
  while((ICM_REGS->ICM_SR & ICM_SR_ENABLE_Msk) == 0);

  // wait for hash to finish
  while((ICM_REGS->ICM_ISR & ICM_ISR_RHC_Msk) == 0);
  
  // copy digest to result buffer
  for(int i=0; i<32; i++)
  {
    result[i] = digest[i];
  }

  // disable ICM
  ICM_REGS->ICM_CTRL = ICM_CTRL_DISABLE_Msk;
  while((ICM_REGS->ICM_SR & ICM_SR_ENABLE_Msk) != 0);
}
