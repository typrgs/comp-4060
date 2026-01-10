#include "common.h"
#include "spi.h"
#include "heart.h"

void spiActivate(uint8_t baud, bool rx, bool idleHigh, bool leadingSample)
{
  uint32_t idleFlag = (idleHigh ? SERCOM_SPIM_CTRLA_CPOL_IDLE_HIGH : SERCOM_SPIM_CTRLA_CPOL_IDLE_LOW);
  uint32_t phaseFlag = (leadingSample ? SERCOM_SPIM_CTRLA_CPHA_LEADING_EDGE : SERCOM_SPIM_CTRLA_CPHA_TRAILING_EDGE);
  uint32_t recvFlag = (rx ? SERCOM_SPIM_CTRLB_RXEN_Msk : 0);
  if (baud == 0)
    baud = 1;

  SERCOM1_REGS->SPIM.SERCOM_CTRLB = SERCOM_SPIM_CTRLB_CHSIZE_8_BIT | recvFlag;

  while((SERCOM1_REGS->SPIM.SERCOM_SYNCBUSY) != 0U)
    ;/* Wait for synchronization */

  SERCOM1_REGS->SPIM.SERCOM_BAUD = (uint8_t)SERCOM_SPIM_BAUD_BAUD(baud);

  SERCOM1_REGS->SPIM.SERCOM_CTRLA = SERCOM_SPIM_CTRLA_DOPO_PAD0 | SERCOM_SPIM_CTRLA_DIPO_PAD3 | SERCOM_SPIM_CTRLA_MODE_SPI_MASTER | idleFlag | phaseFlag | SERCOM_SPIM_CTRLA_DORD_MSB | SERCOM_SPIM_CTRLA_ENABLE_Msk;

  while((SERCOM1_REGS->SPIM.SERCOM_SYNCBUSY) != 0)
    ;/* Wait for synchronization */

}

void spiDeactivate()
{
  SERCOM1_REGS->SPIM.SERCOM_CTRLA = 0;
  SERCOM1_REGS->SPIM.SERCOM_CTRLB = 0;

  while((SERCOM1_REGS->SPIM.SERCOM_SYNCBUSY) != 0)
    ;/* Wait for synchronization */
}

// NOTE: we use SERCOM1 as then PAD[0] is data out and PAD[1] is SCK (allowing pin assignments to be defaults)
void spiInit()
{
  // use generic clock 3 as our clock for this device, running at 48MHz
  GCLK_REGS->GCLK_GENCTRL[3] = GCLK_GENCTRL_DIV(1) | GCLK_GENCTRL_SRC_DFLL | GCLK_GENCTRL_GENEN_Msk;
  while((GCLK_REGS->GCLK_SYNCBUSY & GCLK_SYNCBUSY_GENCTRL_GCLK3) == GCLK_SYNCBUSY_GENCTRL_GCLK3)
    ;/* Wait for synchronization */

  // have to enable the peripheral clocks I need via the generic and main clocks
  // see page 156 of data sheet for GCLK array offsets
  GCLK_REGS->GCLK_PCHCTRL[8] = GCLK_PCHCTRL_GEN(3) | GCLK_PCHCTRL_CHEN_Msk;
  while ((GCLK_REGS->GCLK_PCHCTRL[8] & GCLK_PCHCTRL_CHEN_Msk) != GCLK_PCHCTRL_CHEN_Msk)
    ;/* Wait for synchronization */

  MCLK_REGS->MCLK_APBAMASK |= MCLK_APBAMASK_SERCOM1_Msk;

  // configure pins PA16, PA17, and PA19, used by SERCOM1, to transmit/recv data over SPI
  PORT_REGS->GROUP[0].PORT_PINCFG[16] |= PORT_PINCFG_PMUXEN_Msk;
  PORT_REGS->GROUP[0].PORT_PMUX[8] |= PORT_PMUX_PMUXE_C;
  PORT_REGS->GROUP[0].PORT_PINCFG[17] |= PORT_PINCFG_PMUXEN_Msk;
  PORT_REGS->GROUP[0].PORT_PMUX[8] |= PORT_PMUX_PMUXO_C;
  PORT_REGS->GROUP[0].PORT_PINCFG[19] |= PORT_PINCFG_PMUXEN_Msk | PORT_PINCFG_INEN_Msk;
  PORT_REGS->GROUP[0].PORT_PMUX[9] |= PORT_PMUX_PMUXO_C;
}

uint8_t spiReadByte(uint16_t timeoutMS)
{
  uint32_t value = 0;
  uint32_t currTime = elapsedMS();
  uint32_t endTime = currTime + (uint32_t)timeoutMS;

  // wait until byte is ready
  while ((SERCOM1_REGS->SPIM.SERCOM_INTFLAG & SERCOM_SPIM_INTFLAG_RXC_Msk) != SERCOM_SPIM_INTFLAG_RXC_Msk && (timeoutMS==0 || endTime>currTime))
    currTime = elapsedMS();

  value = SERCOM1_REGS->SPIM.SERCOM_DATA;

  // always return 0 if we timeout
  if (timeoutMS!=0 && endTime<=currTime)
    value = 0;

  return (uint8_t)value;
}

void spiWriteByte(uint8_t value)
{
  SERCOM1_REGS->SPIM.SERCOM_STATUS = (uint16_t)SERCOM_SPIM_STATUS_BUFOVF_Msk;
  SERCOM1_REGS->SPIM.SERCOM_INTFLAG = (uint8_t)SERCOM_SPIM_INTFLAG_ERROR_Msk;

  SERCOM1_REGS->SPIM.SERCOM_DATA = value;

  // wait until done
  while ((SERCOM1_REGS->SPIM.SERCOM_INTFLAG & SERCOM_SPIM_INTFLAG_TXC_Msk) == 0)
    ;
}
