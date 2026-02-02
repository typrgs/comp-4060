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

#define BLINK_RATE 500 // ms
#define PULSE_RATE 1000 // ms
#define CONSENSUS_RATE 10000 // ms

// store a transmit buffer per message type
static CANTxBuf txBufs[NUM_MSG_TYPES] = {0};

// store an acceptance filter per message type
static CANExtFilter filters[NUM_MSG_TYPES] = {0};

static uint8_t myID;
static uint32_t activePeers[UINT8_MAX] = {0};

static Block blockchain[UINT8_MAX * 2] = {0};
static uint16_t height = 0;

static void readParams()
{
  // save device ID given in flash parameters
  uint8_t *params = (uint8_t *)0x000fe000;
  myID = params[0];

  // this parameter indicates whether this node should define the genesis block on startup
  if(params[1])
  {
    blockchain[0].nonce = UINT32_MAX;
    blockchain[0].transaction.amt = 0;
    blockchain[0].transaction.srcID = 0;
    blockchain[0].transaction.destID = 0;
    height++;
  }
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
    txBufs[i].data[0] = myID;
    
    if(i == BLOCK || i == NEW_BLOCK)
    {
      txBufs[i].dataLength += sizeof(Block);
    }

    CANUpdateTxBuf(txBufs[i]);
  }
}


static void markActivePeer(uint8_t peerID)
{
  if(!activePeers[peerID])
  {
    activePeers[peerID] = elapsedMS();
  }
}


static void rxCallback(uint8_t len, uint32_t id)
{
  if(id == PULSE)
  {
    dbg_write_str("Pulse from: ");
    dbg_write_u8(rxBuf, len);
    dbg_write_char('\n');

    // mark peer as active
    markActivePeer(rxBuf[0]);
  }
  else if(id == CONSENSUS)
  {
    uint8_t srcID = rxBuf[0];

    markActivePeer(srcID);

    dbg_write_str("Sharing blocks with ");
    dbg_write_u8(&srcID, 1);
    dbg_write_char('\n');
    
    // setup ack buffer to send to src peer
    uint8_t *idPtr = (uint8_t *)&(txBufs[ACK].id);
    idPtr[0] = ACK;
    idPtr[1] = srcID;
    txBufs[ACK].dataLength = 1;
    txBufs[ACK].data[0] = myID;
    CANSend((1 << ACK));
  }
}

static void peerCheck(uint32_t timeNow)
{
  for(int i=0; i<UINT8_MAX; i++)
  {
    // check if the peer has been recently active, and if they have sent a pulse within 2 pulse periods
    if(activePeers[i] && (timeNow - activePeers[i] > (PULSE_RATE * 2)))
    {
      activePeers[i] = 0;
    }
  }
}

static void consensus()
{

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
  uint32_t pulseTimestamp = 0;
  uint32_t consensusTimestamp = 0;
  uint32_t peerCheckTimestamp = 0;

  for(;;)
  {
    uint32_t msCount = elapsedMS();

    if(msCount >= pulseTimestamp)
    {
      CANSend((1 << PULSE));
      pulseTimestamp = msCount + PULSE_RATE;
    }
    if(msCount >= consensusTimestamp)
    {
      consensus();
      consensusTimestamp = msCount + CONSENSUS_RATE;
    }
    if(msCount >= peerCheckTimestamp)
    {
      peerCheck(msCount);
      peerCheckTimestamp = msCount + (PULSE_RATE * 2);
    }
    if(msCount >= flashTimestamp)
    {
      PORT_REGS->GROUP[0].PORT_OUTTGL = PORT_PA14;
      flashTimestamp = msCount + BLINK_RATE;
    }
  }

  return 0;
}
