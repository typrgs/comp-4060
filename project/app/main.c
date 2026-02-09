#include "heart.h"
#include "can.h"
#include "icm.h"
#include "net.h"
#include "blockchain.h"

#define EXTENDED_FILTER_COUNT NUM_MSG_TYPES
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
static uint8_t rxBuf[CAN_MESSAGE_SIZE];

#define BLINK_RATE 500 // ms
#define PULSE_RATE 1000 // ms
#define CONSENSUS_RATE 10000 // ms

#define CONSENSUS_RESEND_TIMEOUT 1000 // ms

// store a transmit buffer per message type
static CANTxBuf txBufs[NUM_MSG_TYPES] = {0};

// store an acceptance filter per message type
static CANExtFilter filters[NUM_MSG_TYPES] = {0};

static bool startNode = false;

static uint8_t myID;
static uint32_t activePeers[UINT8_MAX] = {0};

static Block blockchain[UINT8_MAX * 2] = {0};
static uint16_t height = 0;

// state variables
static bool doingConsensus = false;
static bool gettingConsensusBlocks = false;
static uint8_t partialBlock[sizeof(Block)];
static uint8_t partialBlockPos = 0;

static uint32_t blockchainBytesPos = 0;


static void readParams()
{
  // save device ID given in flash parameters
  uint8_t *params = (uint8_t *)0x000fe000;
  myID = params[0];

  // this parameter indicates whether this node should define the genesis block on startup
  if(params[1])
  {
    startNode = true;
  }
}

static void updateTxBuf(MsgType type, uint8_t senderID, uint8_t receiverID, uint8_t header, uint8_t dataLength, uint8_t *data)
{
  txBufs[type].bufIndex = type;
  txBufs[type].id[ID_MSG_TYPE_Pos] = type;
  txBufs[type].id[ID_SENDER_Pos] = senderID;
  txBufs[type].id[ID_RECEIVER_Pos] = receiverID;
  txBufs[type].id[ID_HEADER_Pos] = header;
  txBufs[type].dataLength = dataLength;
  if(data != NULL)
  {
    for(int i=0; i<dataLength; i++)
    {
      txBufs[type].data[i] = data[i];
    }
  }

  CANUpdateTxBuf(txBufs[type]);
}

static void updateFilter(MsgType msgType, uint8_t senderID, uint8_t receiverID, uint8_t header, FilterConfig config)
{
  filters[msgType].filterIndex = msgType;
  filters[msgType].firstID[ID_MSG_TYPE_Pos] = msgType;
  filters[msgType].firstID[ID_SENDER_Pos] = senderID;
  filters[msgType].firstID[ID_RECEIVER_Pos] = receiverID;
  filters[msgType].firstID[ID_HEADER_Pos] = header;
  filters[msgType].config = config;
  filters[msgType].type = CLASSIC;
  CANUpdateFilter(filters[msgType]);
}

static void setupFilters()
{
  // setup pulse, consensus, and block filters with broadcast ID and set as enabled
  for(MsgType i=PULSE; i<SHARE; i++)
  {
    updateFilter(i, BROADCAST_ID, BROADCAST_ID, 0, STF0M);
  }

  // setup all other filters to be initially disabled
  for(MsgType i=SHARE; i<NUM_MSG_TYPES; i++)
  {
    updateFilter(i, BROADCAST_ID, myID, 0, DISABLE);
  }
}

static void setupTxBufs()
{
  updateTxBuf(PULSE, BROADCAST_ID, BROADCAST_ID, 0, 1, &myID);

  for(MsgType i=0; i<NUM_MSG_TYPES; i++)
  {
    updateTxBuf(i, myID, BROADCAST_ID, 0, 0, NULL);
  }
}


static void markActivePeer(uint8_t peerID)
{
  activePeers[peerID] = elapsedMS();
}

static bool verifyBlock(Block toVerify)
{
  if(height == 0 && (toVerify.nonce != UINT32_MAX || toVerify.transaction.amt != 0 || toVerify.transaction.srcID != 0 || toVerify.transaction.destID != 0))
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
  MsgType type = idAsBytes[ID_MSG_TYPE_Pos];
  uint8_t senderID = idAsBytes[ID_SENDER_Pos];
  uint8_t header = idAsBytes[ID_HEADER_Pos] & 0x1F;

  if(type == PULSE)
  {
    dbg_write_str("Pulse from: ");
    dbg_write_u8(&senderID, 1);
    dbg_write_char('\n');

    // mark peer as active
    markActivePeer(rxBuf[0]);
  }
  else if(type == CONSENSUS)
  {
    uint8_t consensusReqID = rxBuf[0];

    markActivePeer(consensusReqID);

    dbg_write_str("Sharing blocks with ");
    dbg_write_u8(&consensusReqID, 1);
    dbg_write_char('\n');

    // setup share filter
    updateFilter(SHARE, consensusReqID, myID, 0, STF0M);
    
    // setup ack buffer to send to src peer
    updateTxBuf(ACK, BROADCAST_ID, consensusReqID, CONSENSUS, 1, &myID);
    CANSend(ACK);
  }
  else if(type == ACK)
  {
    if(header == CONSENSUS)
    {
      gettingConsensusBlocks = true;
      doingConsensus = false;
      uint8_t ackID = rxBuf[0];
      markActivePeer(ackID);

      dbg_write_str("Sharing with ");
      dbg_write_u8(&ackID, 1);
      dbg_write_char('\n');

      // once ack received, setup share tx buffer and filter, send share to begin receiving blocks

      // disable ack filter
      filters[ACK].config = DISABLE;
      CANUpdateFilter(filters[ACK]);

      // setup share buffer
      updateTxBuf(SHARE, myID, ackID, 0, 1, &myID);

      // setup block filter
      updateFilter(BLOCK, ackID, myID, SHARE, STF0M);

      // send share request
      CANSend(SHARE);
    }
    else if(header == BLOCK)
    {
      // on block ack, send next block portion if there is more to send
      uint32_t currBytePos = ((uint32_t *)rxBuf)[0];

      if(currBytePos < sizeof(Block) * height)
      {
        uint8_t *blockchainBytesPtr = (uint8_t *)&blockchain;
        uint32_t oldBytesPos = currBytePos;
  
        // manually place block data into tx buffer data region
        for(int i=0; i<CAN_MESSAGE_SIZE && currBytePos < sizeof(Block) * height; i++)
        {
          txBufs[BLOCK].data[i] = blockchainBytesPtr[currBytePos++];
        }

        // use helper to update other tx buf properties
        updateTxBuf(BLOCK, myID, senderID, SHARE, currBytePos - oldBytesPos, NULL);
        CANSend(BLOCK);
      }
      else
      {
        // if entire blockchain already sent, send end response
        updateTxBuf(END, myID, senderID, CONSENSUS, 0, NULL);
        CANSend(END);
      }
    }
  }
  else if(type == BLOCK)
  {
    dbg_write_str("Receiving block from ");
    dbg_write_u8(&senderID, 1);
    dbg_write_char('\n');
    // build block from received data
    for(int i=0; i<len; i++)
    {
      partialBlock[partialBlockPos++] = rxBuf[i];
      blockchainBytesPos++;
    }

    // check if a full block has been constructed
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

    // send block ack, with current blockchain byte position
    updateTxBuf(ACK, myID, senderID, BLOCK, 4, (uint8_t *)&blockchainBytesPos);
    CANSend(ACK);
  }
  else if(type == SHARE)
  {
    // start sending first block in chain
    updateTxBuf(BLOCK, myID, senderID, SHARE, CAN_MESSAGE_SIZE, (uint8_t *)&blockchain);
    CANSend(BLOCK);
  }
  else if(type == END)
  {
    dbg_write_str("Received all blocks\n");

    gettingConsensusBlocks = false;
    blockchainBytesPos = 0;
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
  updateFilter(ACK, BROADCAST_ID, myID, CONSENSUS, STF0M);

  // setup consensus buffer
  updateTxBuf(CONSENSUS, BROADCAST_ID, BROADCAST_ID, 0, 1, (uint8_t *)&myID);

  doingConsensus = true;

  // broadcast consensus request until we get a response
  while(doingConsensus)
  {
    CANSend(CONSENSUS);
    
    // delay for a bit before re-broadcasting the consensus request
    uint32_t now = elapsedMS();
    while(doingConsensus && (elapsedMS() - now < CONSENSUS_RESEND_TIMEOUT));
  }
  
  // spin here while we receive blocks asynchronously and verify the chain, only finishing once we receive an end message
  while(gettingConsensusBlocks);

  // reset filters for normal operation
  setupFilters();
}

static void startup()
{
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

  // start node initializes blockchain on startup, all others do consensus on startup
  if(startNode)
  {
    blockchain[0].nonce = UINT32_MAX;
    blockchain[0].transaction.amt = 0;
    blockchain[0].transaction.srcID = 0;
    blockchain[0].transaction.destID = 0;
    height++;
  }
  else
  {
    consensus();
  }
}

int main()
{
#ifndef NDEBUG
  for(int i=0; i<1000000; i++);
#endif

  // enable interrupts
  __enable_irq();

  startup();

  // timestamps for event scheduling
  uint32_t flashTimestamp = 0;
  uint32_t pulseTimestamp = 0;
  uint32_t consensusTimestamp = CONSENSUS_RATE;
  uint32_t peerCheckTimestamp = PULSE_RATE * 2;

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
