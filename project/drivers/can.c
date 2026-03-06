#include "can.h"

#define CS_PIN PORT_PB05;

static uint32_t *rxFifo0 = NULL;
static uint32_t *rxFifo1 = NULL;
static uint32_t *txBuf = NULL;
static uint32_t *filterList = NULL;
static CANCallback callback = NULL;

void CANInit(uint32_t *rxFifo0Start, uint32_t *rxFifo1Start, uint32_t *txBufStart, uint32_t *extendedFilterListStart, uint32_t rxFifo0Count, uint32_t rxFifo1Count, uint32_t txBufCount, uint32_t extendedFilterListCount, CANCallback rxCallback)
{
  // save necessary pointers
  rxFifo0 = rxFifo0Start;
  rxFifo1 = rxFifo1Start;
  txBuf = txBufStart;
  filterList = extendedFilterListStart;
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
  PORT_REGS->GROUP[1].PORT_DIRSET = CS_PIN;
  PORT_REGS->GROUP[1].PORT_OUTCLR = CS_PIN;

  // set init bit
  CAN1_REGS->CAN_CCCR = CAN_CCCR_INIT_Msk;
  while ((CAN1_REGS->CAN_CCCR & CAN_CCCR_INIT_Msk) == 0)
    ;

  // enable configuration change
  CAN1_REGS->CAN_CCCR |= CAN_CCCR_CCE_Msk;

  // set DBTP register to reset value
  CAN1_REGS->CAN_DBTP = CAN_DBTP_RESETVALUE;

  // set NBTP register to reset value
  CAN1_REGS->CAN_NBTP = CAN_NBTP_RESETVALUE;

  // set global filter configuration
  CAN1_REGS->CAN_GFC = CAN_GFC_ANFE_REJECT; // reject any non-matching frames

  // set extended ID filter configuration
  CAN1_REGS->CAN_XIDFC = CAN_XIDFC_LSE(extendedFilterListCount) | CAN_XIDFC_FLESA(extendedFilterListStart);

  // configure extended ID AND mask
  CAN1_REGS->CAN_XIDAM = CAN_XIDAM_RESETVALUE; // mask is not active when set to reset value

  // configure Rx FIFO 0
  if (rxFifo0Count > 0 && rxFifo0Start != NULL)
  {
    CAN1_REGS->CAN_RXF0C = CAN_RXF0C_F0S(rxFifo0Count) | CAN_RXF0C_F0SA(rxFifo0Start);

    // configure Rx FIFO 0 element size
    CAN1_REGS->CAN_RXESC |= CAN_RXESC_F0DS_DATA8;

    // enable Rx FIFO 0 new message interrupt
    CAN1_REGS->CAN_IE |= CAN_IE_RF0NE_Msk;

    // enable Rx FIFO 0 interrupt line 0
    CAN1_REGS->CAN_ILE |= CAN_ILE_EINT0_Msk;
  }

  // configure Rx FIFO 1
  if (rxFifo1Count > 0 && rxFifo1Start != NULL)
  {
    CAN1_REGS->CAN_RXF1C = CAN_RXF1C_F1S(rxFifo1Count) | CAN_RXF1C_F1SA(rxFifo1Start) | CAN_RXF1C_F1OM_Msk;

    // configure Rx FIFO 1 element size
    CAN1_REGS->CAN_RXESC |= CAN_RXESC_F1DS_DATA8;

    // enable Rx FIFO 1 new message interrupt
    CAN1_REGS->CAN_IE |= CAN_IE_RF1NE_Msk;

    // enable Rx FIFO 1 interrupt line 0
    CAN1_REGS->CAN_ILE |= CAN_ILE_EINT0_Msk;
  }

  // enable interrupts for CAN1
  NVIC_EnableIRQ(CAN1_IRQn);

  // configure Tx Buffer
  CAN1_REGS->CAN_TXBC = CAN_TXBC_NDTB(txBufCount) | CAN_TXBC_TBSA(txBufStart);

  // configure Tx Buffer element size
  CAN1_REGS->CAN_TXESC = CAN_TXESC_TBDS_DATA8;

#ifndef NDEBUG
  // SET TEST MODE
  // CAN1_REGS->CAN_CCCR |= CAN_CCCR_TEST_Msk;

  // SET EXTERNAL LOOPBACK
  // CAN1_REGS->CAN_TEST = CAN_TEST_LBCK_Msk;
#endif

  CAN1_REGS->CAN_CCCR &= ~CAN_CCCR_INIT_Msk;
  while ((CAN1_REGS->CAN_CCCR & CAN_CCCR_INIT_Msk) != 0)
    ;
}

void CANSend(uint8_t index)
{
  // do a send the buffer described by index
  CAN1_REGS->CAN_TXBAR = (1 << index);
}

bool CANReceive(uint8_t fifoIndex, CANMessage *messageBuf)
{
  bool result = true;

  if ((fifoIndex == 0 || fifoIndex == 1) && messageBuf != NULL)
  {
    uint32_t *wordPointer = NULL;

    if (fifoIndex == 0 && (CAN1_REGS->CAN_RXF0S & CAN_RXF0S_F0FL_Msk) > 0)
    {
      // get the index of the next element to receive
      uint32_t getIndex = (CAN1_REGS->CAN_RXF0S & CAN_RXF0S_F0GI_Msk) >> 8;

      // set acknowledge of receive
      CAN1_REGS->CAN_RXF0A = getIndex;

      // get pointer to FIFO element
      wordPointer = &rxFifo0[getIndex * RX_FIFO_ELEMENT_WORDS];
    }
    else if (fifoIndex == 1 && (CAN1_REGS->CAN_RXF1S & CAN_RXF1S_F1FL_Msk) > 0)
    {
      // get the index of the next element to receive
      uint32_t getIndex = (CAN1_REGS->CAN_RXF1S & CAN_RXF1S_F1GI_Msk) >> 8;

      // set acknowledge of receive
      CAN1_REGS->CAN_RXF1A = getIndex;

      // get pointer to FIFO element
      wordPointer = &rxFifo1[getIndex * RX_FIFO_ELEMENT_WORDS];
    }
    else
    {
      result = false;
    }

    if (result)
    {
      // get message type (from ID)
      *((uint32_t *)messageBuf->id) = wordPointer[0] & 0x1FFFFFFF;

      // mask out data length
      messageBuf->len = (uint8_t)((wordPointer[1] & 0xF0000) >> 0x10);

      // get pointer to data section
      uint8_t *dataPointer = (uint8_t *)(&wordPointer[2]);

      // copy data section to receive buffer
      for (int i = 0; i < messageBuf->len; i++)
      {
        messageBuf->data[i] = dataPointer[i];
      }
    }
  }
  else
  {
    result = false;
  }

  return result;
}

void CAN1_Handler()
{
  if ((CAN1_REGS->CAN_IR & CAN_IR_RF0N_Msk) != 0)
  {
    CAN1_REGS->CAN_IR = CAN_IR_RF0N_Msk;
    callback(0);
  }
  else if ((CAN1_REGS->CAN_IR & CAN_IR_RF1N_Msk) != 0)
  {
    CAN1_REGS->CAN_IR = CAN_IR_RF1N_Msk;
    callback(1);
  }
}

void CANUpdateTxBuf(CANTxBuf buf)
{
  uint32_t *bufToUpdate = (uint32_t *)(&txBuf[TX_BUF_ELEMENT_WORDS * buf.bufIndex]);

  // clear out rows to be updated
  for (int i = 0; i < 4; i++)
  {
    bufToUpdate[i] = 0;
  }

  uint32_t id = *((uint32_t *)buf.id);   // address id field as 32-bits rather than 8-bit array
  id &= 0x1FFFFFFF;                      // clear out top 3 bits of new ID just in case
  bufToUpdate[0] = ((0b010 << 29) | id); // update first row

  // update data length
  bufToUpdate[1] = (((uint32_t)buf.dataLength) << 0x10);

  // update data rows
  bufToUpdate[2] = *((uint32_t *)buf.data);
  bufToUpdate[3] = *((uint32_t *)&buf.data[4]);
}

void CANUpdateFilter(CANExtFilter filter)
{
  uint32_t *filterToUpdate = (uint32_t *)&filterList[EXTENDED_FILTER_WORDS * filter.filterIndex];

  filterToUpdate[0] = (((uint32_t)filter.config) << 29 | (*(uint32_t *)filter.id));
  filterToUpdate[1] = (((uint32_t)filter.type) << 30 | *(uint32_t *)filter.id);
}
