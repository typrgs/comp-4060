#include "can.h"

#define CS_PIN PORT_PA18;

// FIFO elements will be two 32-bit rows of "metadata" and two 32-bit rows of data
#define RX_FIFO_ELEMENT_SIZE 128 // bits
#define RX_FIFO_ELEMENT_DATA_INC 64 // bits

uint32_t *messageRam = NULL;
uint32_t *rxFifo = NULL;
void (*callback)(uint8_t *) = NULL;

void canInit(uint32_t *messageRamStart, uint32_t *rxFifoStart, uint32_t *txBufStart, uint32_t *extendedFilterListStart, uint32_t extendedFilterListCount, canCallback rxCallback)
{
  // save necessary pointers 
  messageRam = messageRamStart;
  rxFifo = rxFifoStart;
  callback = rxCallback;

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

  // enable Rx FIFO new message interrupt
  CAN1_REGS->CAN_IE = CAN_IE_RF0NE_Msk;
  
  // enable FIFO interrupt line 0
  CAN1_REGS->CAN_ILE = CAN_ILE_EINT0_Msk;

  // enable interrupts for CAN1
  NVIC_EnableIRQ(CAN1_IRQn);

  // configure Tx Buffer
  CAN1_REGS->CAN_TXBC = CAN_TXBC_TBSA(txBufStart);

  // configure Tx Buffer element size
  CAN1_REGS->CAN_TXESC = CAN_TXESC_TBDS_DATA8;

  CAN1_REGS->CAN_CCCR = 0;
  while((CAN1_REGS->CAN_CCCR & CAN_CCCR_CCE_Msk) != 0);
}

void canSend(uint32_t mask)
{
  // do a send of all transmit buffers defined in the passed mask
  CAN1_REGS->CAN_TXBAR = mask;
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
  
    // compute the start location of the FIFO element data
    uint8_t *getPointer = (uint8_t *)(rxFifo + (getIndex * RX_FIFO_ELEMENT_SIZE));
    getPointer += RX_FIFO_ELEMENT_DATA_INC;
  
    // invoke callback function and pass received data
    callback(getPointer);
  }
}
