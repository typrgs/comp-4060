#include "can.h"

#define CS_PIN PORT_PA18;

uint32_t *messageRam = NULL;

void canInit(uint32_t *messageRamStart, uint32_t *rxFifoStart, uint32_t *txBufStart, uint32_t *extendedFilterListStart, uint32_t extendedFilterListCount)
{
  // save message RAM start
  messageRam = messageRamStart;

  // configure main clock
  MCLK_REGS->MCLK_APBAMASK |= MCLK_AHBMASK_CAN0_Msk;

  // configure generic clock
  GCLK_REGS->GCLK_GENCTRL[5] = GCLK_GENCTRL_GENEN_Msk | GCLK_GENCTRL_SRC_DFLL | GCLK_GENCTRL_DIV(8); // div 8 for 8MHz clock
  GCLK_REGS->GCLK_PCHCTRL[27] = GCLK_PCHCTRL_CHEN_Msk | GCLK_PCHCTRL_GEN(5);

  // configure IO lines for CAN controller to output to CAN transceiver
  PORT_REGS->GROUP[1].PORT_PMUX[6] |= PORT_PMUX_PMUXE_H;
  PORT_REGS->GROUP[1].PORT_PMUX[6] |= PORT_PMUX_PMUXO_H;
  PORT_REGS->GROUP[1].PORT_PINCFG[12] = PORT_PINCFG_PMUXEN_Msk;
  PORT_REGS->GROUP[1].PORT_PINCFG[13] = PORT_PINCFG_PMUXEN_Msk;

  // setup CAN Click GPIO pins to set standard mode
  PORT_REGS->GROUP[1].PORT_DIRSET = CS_PIN;
  PORT_REGS->GROUP[1].PORT_OUTCLR = CS_PIN;

  // set init bit
  CAN1_REGS->CAN_CCCR = CAN_CCCR_INIT_Msk;
  while((CAN1_REGS->CAN_CCCR & CAN_CCCR_INIT_Msk) == 0);

  // enable configuration change
  CAN1_REGS->CAN_CCCR |= CAN_CCCR_CCE_Msk;

  // set NBTP register to reset value
  CAN1_REGS->CAN_NBTP = CAN_NBTP_RESETVALUE;

  // set global filter configuration
  CAN1_REGS->CAN_GFC = CAN_GFC_ANFE_REJECT; // reject any non-matching frames

  // set extended ID filter configuration
  CAN1_REGS->CAN_XIDFC = CAN_XIDFC_LSE(extendedFilterListCount) | CAN_XIDFC_FLESA(extendedFilterListStart);

  // configure extended ID AND mask
  CAN1_REGS->CAN_XIDAM = CAN_XIDAM_RESETVALUE; // mask is not active when set to reset value

  // configure Rx FIFO 0
  CAN1_REGS->CAN_RXF0C = CAN_RXF0C_F0S(1) | CAN_RXF0C_F0SA(rxFifoStart);

  // configure Rx FIFO element size
  CAN1_REGS->CAN_RXESC = CAN_RXESC_F0DS_DATA8;

  // configure Tx Buffer
  CAN1_REGS->CAN_TXBC = CAN_TXBC_TBSA(txBufStart);

  // configure Tx Buffer element size
  CAN1_REGS->CAN_TXESC = CAN_TXESC_TBDS_DATA8;

  CAN1_REGS->CAN_CCCR = 0;
  while((CAN1_REGS->CAN_CCCR & CAN_CCCR_CCE_Msk) != 0);
}

void canSend(uint8_t *bytes)
{

}
