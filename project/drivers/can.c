#include "can.h"

#define CS_PIN PORT_PA18;

uint32_t *rxFifo = NULL;
uint8_t *rxBytes = NULL;
canCallback callback = NULL;

void canInit(uint32_t *rxFifoStart, uint32_t *txBufStart, uint32_t *extendedFilterListStart, uint32_t extendedFilterListCount, uint8_t *buf, canCallback rxCallback)
{
  // save necessary pointers 
  rxFifo = rxFifoStart;
  rxBytes = buf;
  callback = rxCallback;

  // configure main clock
  MCLK_REGS->MCLK_AHBMASK |= MCLK_AHBMASK_CAN1_Msk;

  // configure generic clock
  GCLK_REGS->GCLK_GENCTRL[5] = GCLK_GENCTRL_GENEN_Msk | GCLK_GENCTRL_SRC_DFLL | GCLK_GENCTRL_DIV(6); // div 6 for 8MHz clock
  GCLK_REGS->GCLK_PCHCTRL[28] = GCLK_PCHCTRL_CHEN_Msk | GCLK_PCHCTRL_GEN(5);

  // configure IO lines for CAN controller to output to CAN transceiver
  PORT_REGS->GROUP[1].PORT_PMUX[6] |= PORT_PMUX_PMUXE_H;
  PORT_REGS->GROUP[1].PORT_PMUX[6] |= PORT_PMUX_PMUXO_H;
  PORT_REGS->GROUP[1].PORT_PINCFG[12] = PORT_PINCFG_PMUXEN_Msk;
  PORT_REGS->GROUP[1].PORT_PINCFG[13] = PORT_PINCFG_PMUXEN_Msk;

  // setup CAN Click GPIO pins to set standard mode
  PORT_REGS->GROUP[0].PORT_DIRSET = CS_PIN;
  PORT_REGS->GROUP[0].PORT_OUTCLR = CS_PIN;

  // set init bit
  CAN1_REGS->CAN_CCCR = CAN_CCCR_INIT_Msk;
  while((CAN1_REGS->CAN_CCCR & CAN_CCCR_INIT_Msk) == 0);

  // enable configuration change
  CAN1_REGS->CAN_CCCR |= CAN_CCCR_CCE_Msk;

  // set DBTP register to reset value
  CAN1_REGS->CAN_DBTP = CAN_DBTP_RESETVALUE;

  // set NBTP register to reset value
  CAN1_REGS->CAN_NBTP = CAN_NBTP_RESETVALUE;

  // set global filter configuration
  CAN1_REGS->CAN_GFC = CAN_GFC_ANFE_RXF0; // reject any non-matching frames

  // set extended ID filter configuration
  CAN1_REGS->CAN_XIDFC = CAN_XIDFC_LSE(extendedFilterListCount) | CAN_XIDFC_FLESA(extendedFilterListStart);

  // configure extended ID AND mask
  CAN1_REGS->CAN_XIDAM = CAN_XIDAM_RESETVALUE; // mask is not active when set to reset value

  // configure Rx FIFO 0
  CAN1_REGS->CAN_RXF0C = CAN_RXF0C_F0S(1) | CAN_RXF0C_F0SA(rxFifoStart);

  // configure Rx FIFO element size
  CAN1_REGS->CAN_RXESC = CAN_RXESC_F0DS_DATA8;

  // enable Rx FIFO new message interrupt
  CAN1_REGS->CAN_IE = CAN_IE_RF0NE_Msk;
  
  // enable FIFO interrupt line 0
  CAN1_REGS->CAN_ILE = CAN_ILE_EINT0_Msk;

  // enable interrupts for CAN1
  NVIC_EnableIRQ(CAN1_IRQn);

  // configure Tx Buffer
  CAN1_REGS->CAN_TXBC = CAN_TXBC_NDTB(1) | CAN_TXBC_TBSA(txBufStart);

  // configure Tx Buffer element size
  CAN1_REGS->CAN_TXESC = CAN_TXESC_TBDS_DATA8;

  // SET TEST MODE
  CAN1_REGS->CAN_CCCR |= CAN_CCCR_TEST_Msk;

  // SET EXTERNAL LOOPBACK
  CAN1_REGS->CAN_TEST = CAN_TEST_LBCK_Msk;

  CAN1_REGS->CAN_CCCR &= ~CAN_CCCR_INIT_Msk;
  while((CAN1_REGS->CAN_CCCR & CAN_CCCR_INIT_Msk) != 0);
}

void canSend(uint32_t mask)
{
  // do a send of all transmit buffers defined in the passed mask
  CAN1_REGS->CAN_TXBAR = mask;
  
  // wait for all transmits to finish
  while((CAN1_REGS->CAN_TXBRP & CAN_TXBRP_Msk) != 0);
}

void CAN1_Handler()
{
  if((CAN1_REGS->CAN_IR & CAN_IR_RF0N_Msk) != 0)
  {
    // clear interrupt
    CAN1_REGS->CAN_IR = CAN_IR_RF0N_Msk;

    // get the index of the next element to receive
    uint32_t getIndex = (CAN1_REGS->CAN_RXF0S & CAN_RXF0S_F0GI_Msk);
    
    // set acknowledge of receive
    CAN1_REGS->CAN_RXF0A = getIndex;
  
    // get pointer to FIFO element 
    uint32_t *wordPointer = &rxFifo[getIndex*4];

    // get pointer to data section
    uint8_t *bytePointer = (uint8_t *)(&wordPointer[2]);

    dbg_write_u8(bytePointer, 8);
  }
}
