#include "heart.h"
#include "can.h"
#include "icm.h"
#include "net.h"
#include "blockchain.h"

#define EXTENDED_FILTER_COUNT 1
#define RX_FIFO_ELEMENT_COUNT 10
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

#define CONSENSUS_RESEND_TIMEOUT 100 // ms

// store a transmit buffer per message type
static CANTxBuf txBufs[NUM_MSG_TYPES] = {0};

// store an acceptance filter per message type
static CANExtFilter filters[NUM_MSG_TYPES] = {0};

static uint8_t myID;
static uint32_t activePeers[UINT8_MAX] = {0};

static Block blockchain[UINT8_MAX * 2] = {0};
static uint16_t height = 0;

// state variables
static bool doingConsensus = false;
static uint8_t consensusPeerID = 0;
static uint8_t partialBlock[sizeof(Block)];
static uint8_t partialBlockPos = 0;


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
  filters[PULSE].firstID[0] = PULSE;
  filters[PULSE].firstID[1] = BROADCAST_ID;
  filters[PULSE].config = STF0M;
  filters[PULSE].type = CLASSIC;
  CANUpdateFilter(filters[PULSE]);

  // consensus filter
  filters[CONSENSUS].filterIndex = CONSENSUS;
  filters[CONSENSUS].firstID[0] = CONSENSUS;
  filters[CONSENSUS].firstID[1] = BROADCAST_ID;
  filters[CONSENSUS].config = STF0M;
  filters[CONSENSUS].type = CLASSIC;
  CANUpdateFilter(filters[CONSENSUS]);

  // set up all other filters to be initially disabled
  for(MsgType i=SHARE; i<NUM_MSG_TYPES; i++)
  {
    filters[i].filterIndex = i;
    filters[i].firstID[1] = i;
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
    txBufs[i].id[0] = i;
    txBufs[i].dataLength = 1;
    txBufs[i].data[0] = myID;
    
    if(i == BLOCK)
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

static bool verifyBlock(Block toVerify)
{
  if(height == 0 && (toVerify.nonce != UINT8_MAX || toVerify.transaction.amt != 0 || toVerify.transaction.srcID != 0 || toVerify.transaction.destID != 0))
  {
    return false;
  }
  else if(height > 0)
  {
    // hash current top block for use in comparison
    uint8_t prevHash[BLOCK_HASH_SIZE];

    // msg length is sizeof(Block) * 2 because the length of a hex string needed to represent the size is twice the total amount of bytes
    // (each byte is represented by 8 bits = 2 hex digits)
    icmSHA256((uint8_t *)&(blockchain[height]), sizeof(Block) * 2, prevHash);

    for(int i=0; i<BLOCK_HASH_SIZE; i++)
    {
      if(toVerify.prevHash[i] != prevHash[i])
      {
        return false;
      }
    }
  }

  return true;
}


static void rxCallback(uint8_t len, uint32_t id)
{
  uint8_t *idAsBytes = (uint8_t *)&id;
  MsgType type = idAsBytes[0];
  uint8_t header = idAsBytes[2];

  if(type == PULSE)
  {
    dbg_write_str("Pulse from: ");
    dbg_write_u8(rxBuf, len);
    dbg_write_char('\n');

    // mark peer as active
    markActivePeer(rxBuf[0]);
  }
  else if(type == CONSENSUS)
  {
    uint8_t srcID = rxBuf[0];

    markActivePeer(srcID);

    dbg_write_str("Sharing blocks with ");
    dbg_write_u8(&srcID, 1);
    dbg_write_char('\n');
    
    // setup ack buffer to send to src peer
    txBufs[ACK].id[0] = ACK;
    txBufs[ACK].id[1] = srcID;
    txBufs[ACK].id[2] = CONSENSUS;
    txBufs[ACK].dataLength = 1;
    txBufs[ACK].data[0] = myID;

    // send ack
    CANSend(ACK);
  }
  else if(type == ACK)
  {
    uint8_t srcID = rxBuf[0];

    if(header == CONSENSUS)
    {
      markActivePeer(srcID);

      // save peer ID for upcoming communications
      consensusPeerID = srcID;
    }
    else if(header == BLOCK)
    {
      // on block ack, send next block portion

      // if last portion already sent, send end response
    }
  }
  else if(type == BLOCK)
  {
    // build block from received data
    for(int i=1; i<len; i++)
    {
      partialBlock[partialBlockPos++] = rxBuf[i];
    }

    if(partialBlockPos == sizeof(Block))
    {
      // add block to chain if verification returns true
      Block tempBlock = *((Block *)partialBlock);
      if(verifyBlock(tempBlock))
      {
        blockchain[height++] = tempBlock;
      }

      // reset partial block buffer
      for(int i=0; i<sizeof(Block); i++)
      {
        partialBlock[i] = 0;
      }
      partialBlockPos = 0;
    }
  }
  else if(type == SHARE)
  {
    // send first block in chain
  }
  else if(type == END)
  {
    doingConsensus = false;
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
  // initially, disable all filters while doing consensus
  for(int i=0; i<NUM_MSG_TYPES; i++)
  {
    filters[i].config = DISABLE;
  }

  // setup ack filter
  filters[ACK].firstID[0] = ACK;
  filters[ACK].firstID[1] = myID;
  filters[ACK].config = STF0M;
  filters[ACK].type = CLASSIC;
  CANUpdateFilter(filters[ACK]);

  // setup consensus buffer
  txBufs[CONSENSUS].id[0] = CONSENSUS;
  txBufs[CONSENSUS].id[1] = BROADCAST_ID;
  txBufs[CONSENSUS].dataLength = 1;
  txBufs[CONSENSUS].data[0] = myID;
  CANUpdateTxBuf(txBufs[CONSENSUS]);

  // broadcast consensus request until we get a response
  while(consensusPeerID == 0)
  {
    CANSend(CONSENSUS);
    
    // delay for a bit before re-broadcasting the consensus request
    uint32_t now = elapsedMS();
    while((consensusPeerID == 0) && (elapsedMS() - now < CONSENSUS_RESEND_TIMEOUT));
  }
  
  // once ack received, setup share tx buffer and filter, send share to begin receiving blocks

  // disable ack filter
  filters[ACK].config = DISABLE;
  CANUpdateFilter(filters[ACK]);

  // setup share buffer
  txBufs[SHARE].id[0] = SHARE;
  txBufs[SHARE].id[1] = consensusPeerID;
  txBufs[SHARE].dataLength = 1;
  txBufs[SHARE].data[0] = myID;
  CANUpdateTxBuf(txBufs[SHARE]);

  // setup block filter
  filters[BLOCK].firstID[0] = BLOCK;
  filters[BLOCK].firstID[1] = myID;
  filters[BLOCK].config = STF0M;
  filters[BLOCK].type = CLASSIC;
  CANUpdateFilter(filters[BLOCK]);

  // send share request
  CANSend(SHARE);

  // spin here while we receive blocks asynchronously and verify the chain, only finishing once we receive an end message
  while(doingConsensus);

  // reset filters for normal operation
  setupFilters();
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
      CANSend(PULSE);
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
