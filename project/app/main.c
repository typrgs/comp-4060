#include "heart.h"
#include "can.h"
#include "icm.h"
#include "net.h"
#include "blockchain.h"

#define EXTENDED_FILTER_COUNT 1
#define RX_FIFO_ELEMENT_COUNT 3
#define TX_BUF_ELEMENT_COUNT NUM_MSG_TYPES

// setup message RAM for CAN
#define EXTENDED_FILTER_SIZE EXTENDED_FILTER_COUNT * EXTENDED_FILTER_WORDS
#define RX_FIFO_SIZE RX_FIFO_ELEMENT_COUNT * RX_FIFO_ELEMENT_WORDS
#define TX_BUF_SIZE TX_BUF_ELEMENT_COUNT * TX_BUF_ELEMENT_WORDS
static uint32_t messageRAM[EXTENDED_FILTER_SIZE + RX_FIFO_SIZE + TX_BUF_SIZE] __ALIGNED(32);

// setup start locations of necessary structures
static uint32_t *extendedFilterStart = (uint32_t *)&(messageRAM[0]);
static uint32_t *rxFifoStart = (uint32_t *)&(messageRAM[EXTENDED_FILTER_SIZE]);
static uint32_t *txBufStart = (uint32_t *)&(messageRAM[EXTENDED_FILTER_SIZE + RX_FIFO_SIZE]);
static uint8_t rxBuf[RX_FIFO_ELEMENT_DATA_BYTES];

// store a transmit buffer per message type
static CANTxBuf txBufs[NUM_MSG_TYPES] = {0};

// store an acceptance filter per message type
static CANExtFilter filters[NUM_MSG_TYPES] = {0};

static uint8_t deviceID;


static void rxCallback(uint8_t len, uint32_t id)
{
  if(id == PULSE)
  {
    dbg_write_str("Pulse from: ");
    dbg_write_u8(rxBuf, len);
    dbg_write_char('\n');
  }
  else if(id == CONSENSUS)
  {
    dbg_write_str("Sharing blocks with ");
    dbg_write_u8(rxBuf, len);
    dbg_write_char('\n');
  }
}

static void readParams()
{
  // save device ID given in flash parameters
  uint8_t *params = (uint8_t *)0x000fe000;
  deviceID = params[0];
}

static void setupFilters()
{
  // pulse filter
  filters[PULSE].filterIndex = PULSE;
  filters[PULSE].firstID = PULSE;
  filters[PULSE].config = STF0M;
  filters[PULSE].type = CLASSIC;
  CANUpdateFilter(filters[PULSE]);

  // consensus filter
  filters[CONSENSUS].filterIndex = CONSENSUS;
  filters[CONSENSUS].firstID = CONSENSUS;
  filters[CONSENSUS].config = STF0M;
  filters[CONSENSUS].type = CLASSIC;
  CANUpdateFilter(filters[CONSENSUS]);

  // set up all other filters to be initially disabled
  for(MsgType i=BLOCK; i<NUM_MSG_TYPES; i++)
  {
    filters[i].filterIndex = i;
    filters[i].firstID = i;
    filters[i].config = DISABLE;
    filters[i].type = CLASSIC;
    CANUpdateFilter(filters[i]);
  }
}

static void setupTxBufs()
{
  for(MsgType i=0; i<NUM_MSG_TYPES; i++)
  {
    txBufs[i].bufIndex = i;
    txBufs[i].id = i;
    txBufs[i].dataLength = 1;
    txBufs[i].firstData[0] = deviceID;
    
    if(i == BLOCK || i == NEW_BLOCK)
    {
      txBufs[i].dataLength += sizeof(Block);
    }

    CANUpdateTxBuf(txBufs[i]);
  }
}

int main()
{
#ifndef NDEBUG
  for(int i=0; i<1000000; i++);
#endif

  // enable interrupts
  __enable_irq();
  
  readParams();
  icmInit();
  CANInit(rxFifoStart, NULL, txBufStart, extendedFilterStart, RX_FIFO_ELEMENT_COUNT, 0, TX_BUF_ELEMENT_COUNT, EXTENDED_FILTER_COUNT, rxBuf, rxCallback);
  heartInit();

  // setup filters
  setupFilters();

  // setup transmit buffers
  setupTxBufs();

  // LED output
  PORT_REGS->GROUP[0].PORT_DIRSET = PORT_PA14;
  PORT_REGS->GROUP[0].PORT_OUTSET = PORT_PA14;
  
  uint32_t flashTimestamp = 0;
  
  for(;;)
  {
    uint32_t msCount = elapsedMS();
    
    if(msCount >= flashTimestamp)
    {
      CANSend(3);
      PORT_REGS->GROUP[0].PORT_OUTTGL = PORT_PA14;
      flashTimestamp = msCount + 500;
    }
  }

  return 0;
}
