#include "can.h"
#include "heart.h"

#define EXTENDED_FILTER_COUNT 1
#define RX_FIFO_ELEMENT_COUNT 1
#define TX_BUF_ELEMENT_COUNT 1

#define EXTENDED_FILTER_WORDS 2 // 32-bit words
#define RX_FIFO_ELEMENT_WORDS 4 // 32-bit words
#define TX_BUF_ELEMENT_WORDS 4 // 32-bit words

void callback(uint8_t *recv)
{
  dbg_write_u8(recv, RX_FIFO_ELEMENT_DATA_BYTES);
}

int main()
{
  // setup message RAM for CAN
  uint32_t extendedFilterSize = EXTENDED_FILTER_COUNT * EXTENDED_FILTER_WORDS;
  uint32_t rxFifoSize = RX_FIFO_ELEMENT_COUNT * RX_FIFO_ELEMENT_WORDS;
  uint32_t txBufSize = TX_BUF_ELEMENT_COUNT * TX_BUF_ELEMENT_WORDS;
  
  uint32_t messageRAM[extendedFilterSize + rxFifoSize + txBufSize];

  uint32_t *extendedFilterStart = messageRAM;
  uint32_t *rxFifoStart =  extendedFilterStart + extendedFilterSize;
  uint32_t *txBufStart = rxFifoStart + rxFifoSize;

  // setup extended filters

  // setup TX buffer element

  heartInit();
  canInit(messageRAM, rxFifoStart, txBufStart, extendedFilterStart, EXTENDED_FILTER_COUNT, callback);

  // enable interrupts
  __enable_irq();

  return 0;
}
