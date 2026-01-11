#include "common.h"
#include "twi.h"
#include "heart.h"

void twiActivate(uint8_t baud)
{
  if (baud == 0)
    baud = 1;

  SERCOM2_REGS->I2CM.SERCOM_BAUD = baud;

  // Smart mode enabled (along with SCLSM set to 1), - ACK is set to send while receiving the data
  SERCOM2_REGS->I2CM.SERCOM_CTRLB = SERCOM_I2CM_CTRLB_SMEN_Msk;
  while((SERCOM2_REGS->I2CM.SERCOM_SYNCBUSY) != 0)
    ;/* Wait for synchronization */

  SERCOM2_REGS->I2CM.SERCOM_CTRLA = SERCOM_I2CM_CTRLA_MODE_I2C_MASTER | SERCOM_I2CM_CTRLA_SCLSM(1) |SERCOM_I2CM_CTRLA_ENABLE_Msk;
  while((SERCOM2_REGS->I2CM.SERCOM_SYNCBUSY) != 0)
    ;/* Wait for synchronization */

  // start the state logic
  SERCOM2_REGS->I2CM.SERCOM_STATUS = SERCOM_I2CM_STATUS_BUSSTATE(0x01);
  while((SERCOM2_REGS->I2CM.SERCOM_SYNCBUSY) != 0)
    ;/* Wait for synchronization */
}

void twiDeactivate()
{
  // Reset the unit
  SERCOM2_REGS->I2CM.SERCOM_CTRLA = SERCOM_I2CM_CTRLA_SWRST_Msk;

  while((SERCOM2_REGS->I2CM.SERCOM_SYNCBUSY) != 0)
    ;/* Wait for synchronization */
}

// NOTE: we use SERCOM2 as then PAD[0] is SDA and PAD[1] is SCL (allowing pin assignments to be defaults)
void twiInit()
{
  // use generic clock 4 as our clock for this device, running at 6.86MHz
  GCLK_REGS->GCLK_GENCTRL[4] = GCLK_GENCTRL_DIV(7) | GCLK_GENCTRL_SRC_DFLL | GCLK_GENCTRL_GENEN_Msk;
  while((GCLK_REGS->GCLK_SYNCBUSY & GCLK_SYNCBUSY_GENCTRL_GCLK4) == GCLK_SYNCBUSY_GENCTRL_GCLK4)
    ;/* Wait for synchronization */

  // have to enable the peripheral clocks I need via the generic and main clocks
  // see page 156 of data sheet for GCLK array offsets
  GCLK_REGS->GCLK_PCHCTRL[23] = GCLK_PCHCTRL_GEN(4) | GCLK_PCHCTRL_CHEN_Msk;
  while ((GCLK_REGS->GCLK_PCHCTRL[23] & GCLK_PCHCTRL_CHEN_Msk) != GCLK_PCHCTRL_CHEN_Msk)
    ;/* Wait for synchronization */

  MCLK_REGS->MCLK_APBBMASK |= MCLK_APBBMASK_SERCOM2_Msk;

  // configure pins PA12 and PA13, used by SERCOM2, to transmit/recv data over I2C
  PORT_REGS->GROUP[0].PORT_PINCFG[12] |= PORT_PINCFG_PMUXEN_Msk;
  PORT_REGS->GROUP[0].PORT_PMUX[6] |= PORT_PMUX_PMUXE_C;
  PORT_REGS->GROUP[0].PORT_PINCFG[13] |= PORT_PINCFG_PMUXEN_Msk;
  PORT_REGS->GROUP[0].PORT_PMUX[6] |= PORT_PMUX_PMUXO_C;
}

static void sendAddr(uint8_t addr, bool read)
{
  if (read)
    SERCOM2_REGS->I2CM.SERCOM_ADDR = ((addr<<1) | (uint8_t)0x01);
  else
    SERCOM2_REGS->I2CM.SERCOM_ADDR = (addr<<1);

  while((SERCOM2_REGS->I2CM.SERCOM_SYNCBUSY) != 0)
    ;/* Wait for synchronization */

  // wait for ACK, but only when we're writing
  while (!read && ((SERCOM2_REGS->I2CM.SERCOM_INTFLAG & SERCOM_I2CM_INTFLAG_MB_Msk) == 0))
    ;
}

// returns whether or not the device with the given address is on the bus
bool twiProbe(uint8_t addr)
{
  bool rc = false;

  SERCOM2_REGS->I2CM.SERCOM_ADDR = (addr<<1);

  while((SERCOM2_REGS->I2CM.SERCOM_SYNCBUSY) != 0)
    ;/* Wait for synchronization */

  // wait for ACK, but only when we're writing
  while (((SERCOM2_REGS->I2CM.SERCOM_INTFLAG & SERCOM_I2CM_INTFLAG_MB_Msk) == 0))
    ;

  if ((SERCOM2_REGS->I2CM.SERCOM_STATUS & SERCOM_I2CM_STATUS_RXNACK_Msk) == 0)
    rc = true;

  // issue a stop condition to clear the bus since we don't have an actual transaction
  SERCOM2_REGS->I2CM.SERCOM_CTRLB = SERCOM_I2CM_CTRLB_CMD(0x3);

  return rc;
}

void twiWriteBytes(uint8_t addr, uint8_t const * const bytes, uint16_t size, bool repeatStart)
{
  sendAddr(addr, false);

  for (int i=0; i<size; i++)
  {
    SERCOM2_REGS->I2CM.SERCOM_DATA = bytes[i];

    // wait for ACK
    while ((SERCOM2_REGS->I2CM.SERCOM_INTFLAG & SERCOM_I2CM_INTFLAG_MB_Msk) == 0)
      ;
  }

  //done, issue stop condition if we're not continuing
  if (!repeatStart)
    SERCOM2_REGS->I2CM.SERCOM_CTRLB |= SERCOM_I2CM_CTRLB_CMD(0x3);
}

void twiReadBytes(uint8_t addr, uint8_t * const bytes, uint8_t size)
{
  uint32_t currTime;
  uint32_t endTime;

  sendAddr(addr, true);

  for (int i=0; i<size; i++)
  {
    if (i == (size-1))
    {
      // done, issue a NACK after the last byte
      SERCOM2_REGS->I2CM.SERCOM_CTRLB |= SERCOM_I2CM_CTRLB_ACKACT_Msk | SERCOM_I2CM_CTRLB_CMD(0x3);
      while((SERCOM2_REGS->I2CM.SERCOM_SYNCBUSY) != 0)
        ;/* Wait for synchronization */
    }

   currTime = elapsedMS();
   endTime = currTime + 1;

   // wait for the next byte
    while ((SERCOM2_REGS->I2CM.SERCOM_INTFLAG & SERCOM_I2CM_INTFLAG_SB_Msk) == 0 && endTime>currTime)
      currTime = elapsedMS();

    // issue an ACK by getting the byte
    bytes[i] = (uint8_t)SERCOM2_REGS->I2CM.SERCOM_DATA;
  }

   currTime = elapsedMS();
   endTime = currTime + 1;

  // wait for the next byte, since the flag must be set before issuing a stop command
  while ((SERCOM2_REGS->I2CM.SERCOM_INTFLAG & SERCOM_I2CM_INTFLAG_SB_Msk) == 0 && endTime>currTime)
    currTime = elapsedMS();

  // signal a stop as the transaction is over
  SERCOM2_REGS->I2CM.SERCOM_CTRLB = SERCOM_I2CM_CTRLB_CMD(0x3);
}
