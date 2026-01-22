#include "common.h"
#include "can.h"
#include "heart.h"

#define EXTENDED_FILTER_COUNT 1
#define RX_FIFO_ELEMENT_COUNT 1
#define TX_BUF_ELEMENT_COUNT 1

#define WORD_SIZE 32 // bits

// setup message RAM for CAN
#define EXTENDED_FILTER_SIZE EXTENDED_FILTER_COUNT * EXTENDED_FILTER_WORDS
#define RX_FIFO_SIZE RX_FIFO_ELEMENT_COUNT * RX_FIFO_ELEMENT_WORDS
#define TX_BUF_SIZE TX_BUF_ELEMENT_COUNT * TX_BUF_ELEMENT_WORDS

uint32_t messageRAM[EXTENDED_FILTER_SIZE + RX_FIFO_SIZE + TX_BUF_SIZE] __ALIGNED(32);

uint32_t *extendedFilterStart = (uint32_t *)&(messageRAM[0]);
uint32_t *rxFifoStart = (uint32_t *)&(messageRAM[EXTENDED_FILTER_SIZE]);
uint32_t *txBufStart = (uint32_t *)&(messageRAM[EXTENDED_FILTER_SIZE + RX_FIFO_SIZE]);

uint8_t rxBuf[RX_FIFO_ELEMENT_DATA_BYTES];


void processMsg(uint8_t len)
{
  dbg_write_u8(rxBuf, len);
}

int main()
{
  for(int i=0; i<1000000; i++);

  // enable interrupts
  __enable_irq();

  // setup extended filter
  extendedFilterStart[0] = 0b00100000000000000000000000000001; // store in FIFO 0 on match
  extendedFilterStart[1] = 0b01000000000000000000000000000001; // dual filter (but using same ID)
  
  // setup TX buffer element
  txBufStart[0] = 0b01000000000000000000000000000001;
  txBufStart[1] = 0b00000000000010000000000000000000;
  txBufStart[2] = 0xFFFFFFFF;
  txBufStart[3] = 0xFFFFFFFF;
  
  canInit(rxFifoStart, txBufStart, extendedFilterStart, RX_FIFO_ELEMENT_COUNT, TX_BUF_ELEMENT_COUNT, EXTENDED_FILTER_COUNT, rxBuf, processMsg);
  
  heartInit();

  // LED output
  PORT_REGS->GROUP[0].PORT_DIRSET = PORT_PA14;
  PORT_REGS->GROUP[0].PORT_OUTSET = PORT_PA14;
  
  uint32_t flashTimestamp = 0;
  
  for(;;)
  {
    uint32_t msCount = elapsedMS();
    
    if(msCount >= flashTimestamp)
    {
      canSend(0xFFFFFFFF);
      PORT_REGS->GROUP[0].PORT_OUTTGL = PORT_PA14;
      flashTimestamp = msCount + 500;
    }
  }

  return 0;
}
