#include "icm.h"

static icm_descriptor_registers_t transferDesc0 __ALIGNED(64) = {0};
static icm_descriptor_registers_t transferDesc1 __ALIGNED(64) = {0};
static uint8_t digest[SHA256_DIGEST_SIZE] __ALIGNED(128); // hash area is 32 bytes to hold a SHA256 digest
static uint8_t hashData[SHA256_BLOCK_SIZE * 2];              // data to hash needs to be padded so it is 512 bits long

void icmInit()
{
  // enable main clock
  MCLK_REGS->MCLK_AHBMASK |= MCLK_AHBMASK_ICM_Msk;
  MCLK_REGS->MCLK_APBCMASK |= MCLK_APBCMASK_ICM_Msk;

  // set algorithm to SHA256
  ICM_REGS->ICM_CFG = ICM_CFG_UALGO_SHA256;

  // set start address of transfer descriptor
  ICM_REGS->ICM_DSCR = (uint32_t)&transferDesc0;

  // set start address of hash area
  ICM_REGS->ICM_HASH = (uint32_t)(&digest[0]);

  // set start address of data region
  transferDesc0.ICM_RADDR = (uint32_t)(&hashData[0]);
  transferDesc0.ICM_RNEXT = (uint32_t)(&transferDesc1);

  transferDesc1.ICM_RADDR = (uint32_t)(&hashData[SHA256_BLOCK_SIZE]);

  // set SHA256 algorithm, shortest processing delay
  transferDesc0.ICM_RCFG = ICM_RCFG_ALGO(1) | ICM_RCFG_PROCDLY_SHORT;
  transferDesc1.ICM_RCFG = ICM_RCFG_EOM_Msk;
}

static void padMsg(uint8_t *msg, uint64_t msgLen)
{
  uint8_t dataPos = 0;

  // reset hash data regions
  for (uint8_t i=0; i<SHA256_BLOCK_SIZE * 2; i++)
  {
    hashData[i] = 0;
  }

  // copy message to hash data areas
  for(uint8_t i=0; i<msgLen; i++)
  {
    hashData[i] = msg[i];
  }

  // append a 1
  hashData[dataPos++] = 0x80;

  // use last 64 bits (8 bytes) to store the message length, big endian
  // address the length variable in bytes
  uint8_t *lenPtr = (uint8_t *)&msgLen;

  // determine where to put the last 8 bytes in the data array
  // set transfer descriptors accordingly
  if(dataPos <= SHA256_BLOCK_SIZE - 8)
  {
    transferDesc0.ICM_RNEXT = 0;
    transferDesc0.ICM_RCFG |= ICM_RCFG_EOM_Msk;
  }
  else
  {
    dataPos = (SHA256_BLOCK_SIZE * 2) - 8;
    transferDesc0.ICM_RNEXT = (uint32_t)&transferDesc1;
    transferDesc1.ICM_RCFG = ICM_RCFG_EOM_Msk;
  }

  // place msgLen bytes in big endian order
  for (int i = 7; i >= 0; i--)
  {
    hashData[dataPos++] = lenPtr[i];
  }
}

void icmSHA256(uint8_t *msg, uint64_t msgLen, uint8_t *result)
{
  // pad message and place in hash data area
  padMsg(msg, msgLen);

  // enable ICM
  ICM_REGS->ICM_CTRL = ICM_CTRL_ENABLE_Msk;
  while ((ICM_REGS->ICM_SR & ICM_SR_ENABLE_Msk) == 0)
    ;

  // wait for hash to finish
  while ((ICM_REGS->ICM_ISR & ICM_ISR_RHC_Msk) == 0)
    ;

  // copy digest to result buffer
  for (int i = 0; i < SHA256_DIGEST_SIZE; i++)
  {
    result[i] = digest[i];
  }

  // disable ICM
  ICM_REGS->ICM_CTRL = ICM_CTRL_DISABLE_Msk;
  while ((ICM_REGS->ICM_SR & ICM_SR_ENABLE_Msk) != 0)
    ;
}
