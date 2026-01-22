#include "common.h"
#include "can.h"
#include "heart.h"

#define EXTENDED_FILTER_COUNT 1
#define RX_FIFO_ELEMENT_COUNT 1
#define TX_BUF_ELEMENT_COUNT 1

#define WORD_SIZE 32 // bits

#define EXTENDED_FILTER_WORDS 2 // 32-bit words
#define RX_FIFO_ELEMENT_WORDS 4 // 32-bit words
#define TX_BUF_ELEMENT_WORDS 4 // 32-bit words

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
  extendedFilterStart[0] = 0b00100000000000000000000000000001;
  extendedFilterStart[1] = 0b10000000000000000000000000000001;
  
  // setup TX buffer element
  txBufStart[0] = 0b11000000000000000000000000000001;
  txBufStart[1] = (0x00 | 0b0 | 0b0 | 0b0 | 0b0 | 0x1 | 0x0000);
  txBufStart[2] = 0xF0 | 0x00 | 0x00 | 0xFF;
  
  canInit(rxFifoStart, txBufStart, extendedFilterStart, EXTENDED_FILTER_COUNT, rxBuf, processMsg);
  
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
