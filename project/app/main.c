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
static bool waitingForConsensus = false;
static bool doingConsensus = false;
static uint8_t partialBlock[sizeof(Block)];
static uint8_t partialBlockPos = 0;

static uint32_t blockBytesPos = 0;

static bool receivingNewBlock = false;


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
  filters[msgType].id[ID_MSG_TYPE_Pos] = msgType;
  filters[msgType].id[ID_SENDER_Pos] = senderID;
  filters[msgType].id[ID_RECEIVER_Pos] = receiverID;
  filters[msgType].id[ID_HEADER_Pos] = header;
  filters[msgType].config = config;
  filters[msgType].type = CLASSIC;
  CANUpdateFilter(filters[msgType]);
}

static void setupFilters()
{
  // setup pulse, consensus, and block filters with broadcast ID and set as enabled
  for(MsgType i=PULSE; i<NUM_MSG_TYPES; i++)
  {
    updateFilter(i, BROADCAST_ID, BROADCAST_ID, 0, STF0M);
  }
}

static void setupTxBufs()
{
  updateTxBuf(PULSE, BROADCAST_ID, BROADCAST_ID, 0, 1, &myID);

  for(MsgType i=CONSENSUS; i<NUM_MSG_TYPES; i++)
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

static bool sendBlockMessage(uint16_t heightConsidered, uint8_t *blockBytesPtr, uint64_t currBytePos, uint8_t to, uint8_t from, HeaderType header)
{
  bool result = true;

  // check if there are still bytes to be sent for the block(s) we are sending
  if(currBytePos < sizeof(Block) * heightConsidered)
  {
    uint64_t oldBytesPos = currBytePos;

    // check how many bytes are left until reaching the end of the set of blocks being sent
    uint64_t bytesUntilEnd = (sizeof(Block) * heightConsidered) - currBytePos;

    // advance the current byte position by the min of the bytes remaining and the max message size
    currBytePos += (bytesUntilEnd >= CAN_MESSAGE_SIZE ? CAN_MESSAGE_SIZE : bytesUntilEnd);
  
    // update transmit buffer and send
    updateTxBuf(BLOCK, from, to, header, currBytePos - oldBytesPos, &blockBytesPtr[oldBytesPos]);
    CANSend(BLOCK);
  }
  // send end message if there are no more bytes to send
  else
  {
    result = false;
    updateTxBuf(END, from, to, header, 0, NULL);
    CANSend(END);
  }

  return result;
}

static void rxCallback(uint8_t len, uint32_t id)
{
  uint8_t *idAsBytes = (uint8_t *)&id;
  MsgType type = idAsBytes[ID_MSG_TYPE_Pos];
  uint8_t senderID = idAsBytes[ID_SENDER_Pos];
  uint8_t receiverID = idAsBytes[ID_RECEIVER_Pos];
  uint8_t header = idAsBytes[ID_HEADER_Pos] & 0x1F;

  if(type == PULSE)
  {
    dbg_write_str("Pulse from: ");
    dbg_write_u8(rxBuf, 1);
    dbg_write_char('\n');

    // mark peer as active
    markActivePeer(rxBuf[0]);
  }
  else if(type == CONSENSUS)
  {
    if(header == ACK && (doingConsensus || waitingForConsensus))
    {
      // we've completed the consensus handshake, and now it is time to begin sharing our blockchain
      if(senderID == BROADCAST_ID && receiverID == myID)
      {
        dbg_write_str("Consensus handshake completed\n");

        doingConsensus = true;
        waitingForConsensus = false;
        
        // setup filter to only accept consensus blocks from partner
        uint8_t partnerID = rxBuf[0];
        updateFilter(BLOCK, partnerID, myID, SHARE, STF0M);
        
        // send request for first block
        blockBytesPos = 0;
        updateTxBuf(CONSENSUS, myID, partnerID, ACK, sizeof(blockBytesPos), (uint8_t *)&blockBytesPos);
        CANSend(CONSENSUS);
      }
      // we are continuining to share our blockchain
      else if(senderID != BROADCAST_ID && receiverID == myID)
      {
        dbg_write_str("Beginning to send blocks\n");

        // send blocks, starting from the point they send us
        uint8_t currBytePos = rxBuf[0];
        
        // if we sent an end message instead, reset the consensus filter to receive future requests
        if(!sendBlockMessage(height, (uint8_t *)&blockchain, currBytePos, senderID, myID, SHARE))
        {
          doingConsensus = false;
          updateFilter(CONSENSUS, BROADCAST_ID, BROADCAST_ID, NONE, STF0M);  
        }
      }
    }
    // we have received a consensus request from someone on the network
    else
    {
      uint8_t consensusReqID = rxBuf[0];
      markActivePeer(consensusReqID);

      doingConsensus = true;

      dbg_write_str("Sharing blocks with ");
      dbg_write_u8(&consensusReqID, 1);
      dbg_write_char('\n');
    
      // update consensus filter with partner ID to reject all other consensus requests
      updateFilter(CONSENSUS, consensusReqID, myID, ACK, STF0M);
      
      // setup consensus buffer to send confirmation to src peer
      updateTxBuf(CONSENSUS, BROADCAST_ID, consensusReqID, ACK, 1, &myID);
      CANSend(CONSENSUS);
    }
  }
  else if(type == BLOCK)
  {
    dbg_write_str("Receiving block from ");
    dbg_write_u8(&senderID, 1);
    dbg_write_char('\n');

    if(senderID == BROADCAST_ID && receiverID == BROADCAST_ID && header == NEW)
    {
      receivingNewBlock = true;
    }

    // build block from received data
    for(int i=0; i<len; i++)
    {
      partialBlock[partialBlockPos++] = rxBuf[i];
      blockBytesPos++;
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

    if(header == SHARE)
    {
      updateTxBuf(CONSENSUS, myID, senderID, ACK, sizeof(blockBytesPos), (uint8_t *)&blockBytesPos);
      CANSend(CONSENSUS);
    }
  }
  else if(type == END)
  {
    dbg_write_str("Received all blocks\n");

    if(header == SHARE)
    {
      dbg_write_str("Consensus complete\n");
      doingConsensus = false;
    }
    else if(header == NEW)
    {
      receivingNewBlock = false;
    }
    blockBytesPos = 0;
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
  updateFilter(CONSENSUS, BROADCAST_ID, myID, ACK, STF0M);

  // setup consensus buffer
  updateTxBuf(CONSENSUS, BROADCAST_ID, BROADCAST_ID, 0, 1, (uint8_t *)&myID);

  waitingForConsensus = true;

  // broadcast consensus request until we get a response
  while(waitingForConsensus)
  {
    CANSend(CONSENSUS);
    
    // delay for a bit before re-broadcasting the consensus request
    uint32_t now = elapsedMS();
    while(waitingForConsensus && (elapsedMS() - now < CONSENSUS_RESEND_TIMEOUT));
  }
  
  // spin here while we receive blocks asynchronously and verify the chain, only finishing once we receive an end message
  while(doingConsensus);

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
