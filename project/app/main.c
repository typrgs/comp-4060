#include "heart.h"
#include "can.h"
#include "icm.h"
#include "net.h"
#include "blockchain.h"
#include "trng.h"

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
#define PULSE_RATE 5000 // ms
#define PEER_CHECK_RATE 15000 // ms
#define CONSENSUS_RATE 10000 // ms

#define DISCOVERY_TIMEOUT 1000 // ms

// store a transmit buffer per message type
static CANTxBuf txBufs[NUM_MSG_TYPES] = {0};

// store an acceptance filter per message type
static CANExtFilter filters[NUM_MSG_TYPES] = {0};

static bool startNode = false;

static uint8_t myID;
static bool activePeers[UINT8_MAX] = {0};

static Block blockchain[BLOCKCHAIN_SIZE] = {0};
static uint16_t height = 0;
static uint8_t longestChainPeerID = 0;
static uint16_t longestChainHeight = 0;

static bool doingDiscovery = true;
static bool discoverySuccess = false;
static uint8_t chainPartnerID = 0;
static uint8_t partialBlock[sizeof(Block)];
static uint8_t partialBlockPos = 0;

static uint32_t blockBytesPos = 0;

static bool receivingNewBlock = false;

static void discover();


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
  filters[msgType].type = DUAL;
  CANUpdateFilter(filters[msgType]);
}

static void resetFilters()
{
  for(uint8_t i=0; i<NUM_MSG_TYPES; i++)
  {
    updateFilter(i, BROADCAST_ID, BROADCAST_ID, NONE, STF0M);
  }

  updateFilter(CHAIN, BROADCAST_ID, myID, NONE, STF0M);
}

static void resetTxBufs()
{
  updateTxBuf(PULSE, BROADCAST_ID, BROADCAST_ID, 0, 1, &myID);

  for(MsgType i=CHAIN; i<NUM_MSG_TYPES; i++)
  {
    updateTxBuf(i, myID, BROADCAST_ID, 0, 0, NULL);
  }
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

static bool storePartialBlock(uint8_t *rxBuf, uint8_t len)
{
  bool result = true;

  // build block from received data
  for(int i=0; i<len; i++)
  {
    partialBlock[partialBlockPos++] = rxBuf[i];
    
    // check if a full block has been constructed
    if(partialBlockPos == sizeof(Block))
    {
      // add block to chain if verification returns true
      Block tempBlock = *((Block *)&partialBlock);
      result = verifyBlock(tempBlock);

      if(result)
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
    
    blockBytesPos++;
  }
  
  return result;
}


static void rxPulse(uint8_t senderID, uint8_t receiverID, HeaderType header, uint8_t *rxBuf, uint8_t len)
{
  dbg_write_str("Pulse from: ");
  dbg_write_u8(rxBuf, 1);
  dbg_write_char('\n');

  // mark peer as active
  activePeers[rxBuf[0]] = true;
}

static void rxDiscover(uint8_t senderID, uint8_t receiverID, HeaderType header, uint8_t *rxBuf, uint8_t len)
{
  // someone is joining the network and they want to find out the longest chain
  if(!doingDiscovery && header == NONE)
  {
    dbg_write_str("Received discovery request\n");

    // place ID and height into data buffer
    uint8_t data[sizeof(height) + sizeof(myID)];
    data[0] = myID;
    data[1] = ((uint8_t *)&height)[0];
    data[2] = ((uint8_t *)&height)[1];

    updateFilter(CHAIN, BROADCAST_ID, myID, NONE, STF0M);
    updateTxBuf(DISCOVER, BROADCAST_ID, rxBuf[0], SHARE, sizeof(data), data);
    CANSend(DISCOVER);
  }
  else if(header == SHARE)
  {
    // extract data from buffer
    uint8_t peer = rxBuf[0];
    uint16_t receivedHeight = *(uint16_t *)(&rxBuf[1]);
    
    // save the received height if larger than the currently saved value
    if(receivedHeight >= longestChainHeight)
    {
      longestChainHeight = receivedHeight;
      longestChainPeerID = peer;
    }
  }
}

static void rxChain(uint8_t senderID, uint8_t receiverID, HeaderType header, uint8_t *rxBuf, uint8_t len)
{
  // receive NONE, respond ACK
  if(!chainPartnerID && header == NONE)
  {
    dbg_write_str("Received chain request\n");

    // save partner ID, get filter ready for chain sharing requests, send chain ack
    chainPartnerID = rxBuf[0];
    updateFilter(CHAIN, chainPartnerID, myID, SHARE, STF0M);
    updateTxBuf(CHAIN, myID, chainPartnerID, ACK, 0, NULL);
    CANSend(CHAIN);
  }
  // receive ACK, respond SHARE
  else if(header == ACK)
  {
    dbg_write_str("Discovery handshake completed\n");

    // save partner ID, get filter ready for block responses, and send request for first block
    chainPartnerID = senderID;
    updateFilter(CHAIN, chainPartnerID, myID, BLOCK, STF0M);
    updateTxBuf(CHAIN, myID, chainPartnerID, SHARE, sizeof(blockBytesPos), (uint8_t *)&blockBytesPos);
    CANSend(CHAIN);
  }
  // receive SHARE, respond BLOCK
  else if(header == SHARE)
  {
    dbg_write_str("Received chain block request\n");

    // get the currrent byte position from data buffer
    uint32_t oldBytesPos = *(uint32_t *)rxBuf;

    // calculate how many bytes are left until the end of the chain
    uint32_t bytesUntilEnd = (height * sizeof(Block)) - oldBytesPos;

    // if there is more data, send out bytes
    // bytes sent is either the full message size, or whatever might be remaining
    if(bytesUntilEnd > 0)
    {
      uint32_t newBytesPos = oldBytesPos + (bytesUntilEnd > CAN_MESSAGE_SIZE ? CAN_MESSAGE_SIZE : bytesUntilEnd);
  
      updateTxBuf(CHAIN, myID, chainPartnerID, BLOCK, newBytesPos - oldBytesPos, &((uint8_t *)&blockchain)[oldBytesPos]);
    }
    // send block response with length of 0 to indicate completion
    else
    {
      dbg_write_str("Full chain sent\n");

      updateTxBuf(CHAIN, myID, chainPartnerID, BLOCK, 0, NULL);
      chainPartnerID = 0;
      resetFilters();
    }

    CANSend(CHAIN);
  }
  // receive BLOCK, respond SHARE
  else if(header == BLOCK)
  {
    dbg_write_str("Received discovery block\n");

    // no more data sent, meaning discovery is done
    if(len == 0)
    {
      dbg_write_str("Completed discovery\n");
      dbg_write_u32(&blockchain[0].nonce, 1);
      dbg_write_u16(&height, 1);

      chainPartnerID = 0;
      longestChainPeerID = 0;
      longestChainHeight = 0;
      blockBytesPos = 0;
      doingDiscovery = false;
      discoverySuccess = true;
      resetFilters();
    }
    // we successfully receive the data into a partial block buffer, so we can send a request for the next set of bytes
    else if(storePartialBlock(rxBuf, len))
    {
      updateTxBuf(CHAIN, myID, chainPartnerID, SHARE, sizeof(blockBytesPos), (uint8_t *)&blockBytesPos);
      CANSend(CHAIN);
    }
    // the block we received didn't validate properly, so we try discovery again
    else
    {
      updateTxBuf(CHAIN, myID, chainPartnerID, ERROR, 0, NULL);
      CANSend(CHAIN);

      chainPartnerID = 0;
      longestChainPeerID = 0;
      longestChainHeight = 0;
      blockBytesPos = 0;
      doingDiscovery = false;
      discoverySuccess = false;
      resetFilters();
    }
  }
  // our chain is invalid, so we need to discover a new chain
  else if(header == ERROR)
  {
    chainPartnerID = 0;

    // clear out blockchain
    Block empty = {0};
    for(uint16_t i=0; i<BLOCKCHAIN_SIZE; i++)
    {
      blockchain[i] = empty;
    }
    height = 0;

    // do discovery to get a valid chain
    discover();
  }
}

static void rxNew(uint8_t senderID, uint8_t receiverID, HeaderType header, uint8_t *rxBuf, uint8_t len)
{

}

static void rxCallback(uint8_t len, uint32_t id)
{
  uint8_t *idAsBytes = (uint8_t *)&id;
  MsgType type = idAsBytes[ID_MSG_TYPE_Pos];
  uint8_t senderID = idAsBytes[ID_SENDER_Pos];
  uint8_t receiverID = idAsBytes[ID_RECEIVER_Pos];
  HeaderType header = idAsBytes[ID_HEADER_Pos] & 0x1F;

  static void (*rxTypes[])(uint8_t, uint8_t, HeaderType, uint8_t *, uint8_t) = {rxPulse, rxDiscover, rxChain, rxNew};

  rxTypes[type](senderID, receiverID, header, rxBuf, len);
}

static void discover()
{
  discoverySuccess = false;
  chainPartnerID = 0;
  doingDiscovery = true;
  uint32_t now;

  // keep trying to discover until we successfully get a valid chain
  while(!discoverySuccess)
  {
    updateFilter(DISCOVER, BROADCAST_ID, myID, SHARE, STF0M);
    updateTxBuf(DISCOVER, BROADCAST_ID, BROADCAST_ID, NONE, 1, &myID);

    // keep sending discovery requests until we get a response from a peer
    while(!longestChainPeerID)
    {
      // ask everyone on network for their chain heights and IDs
      CANSend(DISCOVER);
  
      // wait a bit to get responses
      now = elapsedMS();
      while(!longestChainPeerID && (elapsedMS() - now < DISCOVERY_TIMEOUT));
    }

    dbg_write_u16(&longestChainHeight, 1);
    dbg_write_u8(&longestChainPeerID, 1);

    // ask last peer that responded with the longest chain for their blockchain
    updateFilter(CHAIN, longestChainPeerID, myID, ACK, STF0M);
    updateTxBuf(CHAIN, BROADCAST_ID, longestChainPeerID, NONE, 1, &myID);
    
    // keep sending the chain request, since they may not be ready to share with us if they are currently sharing with another peer
    while(!chainPartnerID)
    {
      CANSend(CHAIN);

      now = elapsedMS();
      while(!chainPartnerID && (elapsedMS() - now < DISCOVERY_TIMEOUT));
    }

    // wait in this loop while receving the chain from our partner peer.
    while(doingDiscovery);
  }

  resetFilters();
}

static void startup()
{
  readParams();

  // start node initializes blockchain on startup, all others do consensus on startup
  if(startNode)
  {
    doingDiscovery = false;
    chainPartnerID = 0;
    discoverySuccess = true;

    blockchain[0].nonce = UINT32_MAX;
    blockchain[0].transaction.amt = 0;
    blockchain[0].transaction.srcID = 0;
    blockchain[0].transaction.destID = 0;
    height++;
  }
  else
  {
    doingDiscovery = true;
    chainPartnerID = 0;
    discoverySuccess = false;
  }
  
  icmInit();
  CANInit(rxFifoStart, NULL, txBufStart, extendedFilterStart, RX_FIFO_ELEMENT_COUNT, 0, TX_BUF_ELEMENT_COUNT, EXTENDED_FILTER_COUNT, rxBuf, rxCallback);
  heartInit();
  trngInit();

  // setup filters
  resetFilters();
  
  // setup transmit buffers
  resetTxBufs();
  
  // LED output
  PORT_REGS->GROUP[0].PORT_DIRSET = PORT_PA14;
  PORT_REGS->GROUP[0].PORT_OUTSET = PORT_PA14;
  
  if(!startNode)
  {
    discover();
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

  for(;;)
  {
    uint32_t msCount = elapsedMS();

    if(msCount >= pulseTimestamp)
    {
      CANSend(PULSE);
      pulseTimestamp = msCount + PULSE_RATE;
    }
    if(msCount >= flashTimestamp)
    {
      PORT_REGS->GROUP[0].PORT_OUTTGL = PORT_PA14;
      flashTimestamp = msCount + BLINK_RATE;
    }
  }

  return 0;
}
