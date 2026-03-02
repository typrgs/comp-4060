#include "heart.h"
#include "can.h"
#include "icm.h"
#include "net.h"
#include "blockchain.h"
#include "trng.h"
#include "morse_map.h"
#include "display.h"
#include "spi.h"

#define EXTENDED_FILTER_COUNT NUM_MSG_TYPES
#define RX_FIFO_ELEMENT_COUNT 20
#define TX_BUF_ELEMENT_COUNT NUM_MSG_TYPES

// setup message RAM for CAN
#define EXTENDED_FILTER_SIZE EXTENDED_FILTER_COUNT * EXTENDED_FILTER_WORDS
#define RX_FIFO_SIZE RX_FIFO_ELEMENT_COUNT * RX_FIFO_ELEMENT_WORDS
#define TX_BUF_SIZE TX_BUF_ELEMENT_COUNT * TX_BUF_ELEMENT_WORDS
static uint32_t messageRAM[EXTENDED_FILTER_SIZE + RX_FIFO_SIZE + RX_FIFO_SIZE + TX_BUF_SIZE] __ALIGNED(32);

// setup start locations of necessary structures
static uint32_t *extendedFilterStart = (uint32_t *)&(messageRAM[0]);
static uint32_t *rxFifo0Start = (uint32_t *)&(messageRAM[EXTENDED_FILTER_SIZE]);
static uint32_t *rxFifo1Start = (uint32_t *)&(messageRAM[EXTENDED_FILTER_SIZE + RX_FIFO_SIZE]);
static uint32_t *txBufStart = (uint32_t *)&(messageRAM[EXTENDED_FILTER_SIZE + RX_FIFO_SIZE + RX_FIFO_SIZE]);

#define BLINK_RATE 500 // ms
#define PULSE_RATE 5000 // ms
#define PEER_CHECK_RATE PULSE_RATE * 2 // ms

#define DISCOVERY_TIMEOUT 1000 // ms

#define HYS_ON_MAX 1
#define HYS_ON_LIM 1
#define HYS_OFF_MIN 0
#define HYS_OFF_LIM 0

#define SW0_SAMPLE_RATE 10 // ms
#define PROCESS_TX_RATE 10 // ms

#define HOLD_MIN_TIME 250 // ms
#define MSG_TIMEOUT 1000 // ms
#define LETTER_TIMEOUT 400 // ms

// struct for hysteresis recording and event processing
typedef struct HYS_OBJ
{
  uint32_t accumulator;
  uint32_t lastPress;
  uint32_t lastRelease;
  uint8_t state;
  uint8_t lastState;
  uint8_t event;
  uint8_t lastEvent;
  uint8_t pendingInput;
} HysObj;

typedef enum TX_STATE
{
  TX_IDLE,
  TX_READ,
  TX_CONVERT,
  TX_TRANSMIT,
  TX_NUM_STATES
} TxState;

static TxState txIdle(HysObj);
static TxState txRead(HysObj);
static TxState txConvert(HysObj);
static TxState txTransmit(HysObj);

static TxState (*txStates[])(HysObj) = {txIdle, txRead, txConvert, txTransmit}; 
static TxState currTxState = TX_IDLE;

// hysteresis object for SW0
static HysObj sw0 = {0};

// buffers for building transaction messages
static char txBuf[TRANSACTION_MSG_SIZE] = {0};
static uint8_t txBufPos = 0;
static char currBin[MORSE_MAX_LEN + 1] = {0};
static uint8_t currBinPos = 0;

static uint8_t fifo0Count = 0;
static uint8_t fifo1Count = 0;

// store a transmit buffer per message type
static CANTxBuf txBufs[NUM_MSG_TYPES] = {0};

// store an acceptance filter per message type
static CANExtFilter filters[NUM_MSG_TYPES] = {0};

static bool startNode = false;

static uint8_t myID;
static uint32_t activePeers[UINT8_MAX] = {0};

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

static Block newBlock = {0};
static uint8_t newBlockRxPartnerID = 0;
static uint8_t newBlockTxPartnerID = 0;

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
  updateFilter(NEW_RX, BROADCAST_ID, myID, NONE, STF0M);
}

static void resetTxBufs()
{
  updateTxBuf(PULSE, BROADCAST_ID, BROADCAST_ID, 0, 1, &myID);

  for(MsgType i=CHAIN; i<NUM_MSG_TYPES; i++)
  {
    updateTxBuf(i, myID, BROADCAST_ID, 0, 0, NULL);
  }
}

static bool compareBlocks(Block a, Block b)
{
  if(a.height != b.height) return false;
  if(a.minerID != b.minerID) return false;
  if(a.nonce != b.nonce) return false;
  if(a.transaction.srcID != b.transaction.srcID) return false;
  
  for(uint8_t i=0; i<TRANSACTION_MSG_SIZE; i++)
  {
    if(a.transaction.msg[i] != b.transaction.msg[i]) return false;
  }
  
  for(uint16_t i=0; i<BLOCK_HASH_SIZE; i++)
  {
    if(a.prevHash[i] != b.prevHash[i]) return false;
  }

  return true;
}

static bool findBlock(Block key)
{
  for(uint16_t i=0; i<height; i++)
  {
    if(compareBlocks(blockchain[i], key))
    {
      return true;
    }
  }

  return false;
}

static void printBlock(Block block)
{
  dbg_write_u8(&block.transaction.srcID, 1);
  dbg_write_u8(&block.transaction.msgLen, 1);
  dbg_write_u8((uint8_t *)&block.transaction.msg, block.transaction.msgLen);
  dbg_write_u32(&block.nonce, 1);
  dbg_write_u8(block.prevHash, 3);
  dbg_write_char('\n');
}

static void printChain()
{
  for(int i=0; i<height; i++)
  {
    printBlock(blockchain[i]);
  }
}

static void printRejected()
{
  CANMessage message = {0};
  CANReceive(1, &message);

  dbg_write_str("Rejected: ");
  dbg_write_u8(message.id, 4);
  dbg_write_str(" Type filter: ");
  dbg_write_u8(filters[message.id[ID_MSG_TYPE_Pos]].id, 4);
  dbg_write_char('\n');

  fifo1Count--;
}

static bool verifyBlock(Block toVerify)
{
  if(findBlock(toVerify))
  {
    dbg_write_str("Block already exists\n");
    return false;
  }
  else if(height == 0 && (toVerify.nonce != UINT32_MAX || toVerify.transaction.msgLen != 0 || toVerify.transaction.srcID != 0))
  {
    return false;
  }
  else if(height > 0)
  {
    // hash current top block for use in comparison
    uint8_t prevHash[BLOCK_HASH_SIZE] = {0};

    // msg length is sizeof(Block) * 2 because the length of a hex string needed to represent the size is twice the total amount of bytes
    // (each byte is represented by 8 bits = 2 hex digits)
    icmSHA256((uint8_t *)&(blockchain[height-1]), sizeof(Block), prevHash);

    for(int i=0; i<BLOCK_HASH_SIZE; i++)
    {
      if(toVerify.prevHash[i] != prevHash[i])
      {
        dbg_write_str("Prev hash does not match\n");
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
      Block tempBlock = *((Block *)partialBlock);
      dbg_write_str("Full block constructed ");
      printBlock(tempBlock);
      result = verifyBlock(tempBlock);

      if(result)
      {
        dbg_write_str("New block added ");
        printBlock(tempBlock);

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

static bool sendBlock(uint16_t height, uint8_t *blockBytes, uint32_t bytesPos, MsgType type, uint8_t senderID, uint8_t receiverID)
{
  bool finished = false;

  // calculate how many bytes are left until the end of the chain
  uint32_t bytesUntilEnd = (height * sizeof(Block)) - bytesPos;

  // if there is more data, send out bytes
  // bytes sent is either the full message size, or whatever might be remaining
  if(bytesUntilEnd > 0)
  {
    uint32_t newBytesPos = bytesPos + (bytesUntilEnd > CAN_MESSAGE_SIZE ? CAN_MESSAGE_SIZE : bytesUntilEnd);

    updateTxBuf(type, senderID, receiverID, BLOCK, newBytesPos - bytesPos, &blockBytes[bytesPos]);
  }
  // send block response with length of 0 to indicate completion
  else
  {
    finished = true;
    updateTxBuf(type, senderID, receiverID, BLOCK, 0, NULL);
  }

  CANSend(type);

  return finished;
}

static uint8_t condenseActivePeers(uint32_t *arr, uint8_t *condensedArr)
{
  uint8_t len = 0;

  // squish non-zero arr contents into the start of condensedArr
  for(uint8_t i=0; i<UINT8_MAX; i++)
  {
    if(activePeers[i])
    {
      condensedArr[len++] = i;
    }
  }

  return len;
}

static uint8_t removeFromArray(uint8_t *arr, uint8_t size, uint8_t element)
{
  uint8_t newSize = size;
  int pos = -1;

  for(uint8_t i=0; i<size && pos<0; i++)
  {
    if(arr[i] == element) pos = i;
  }

  if(pos >= 0)
  {
    for(uint8_t i=pos; i<size-1; i++)
    {
      arr[i] = arr[i+1];
      arr[i+1] = 0;
    }

    newSize--;
  }

  return newSize;
}

static void peerCheck(uint32_t *activePeers)
{
  uint32_t now = elapsedMS();

  for(uint8_t i=0; i<UINT8_MAX; i++)
  {
    if(activePeers[i])
    {
      // check if each peer has recently pulsed 
      if(now - activePeers[i] > PULSE_RATE)
      {
        activePeers[i] = 0;
      }
    }
  }
}

static void sendNewestBlock()
{
  // wait for previous transmit to complete
  while(newBlockTxPartnerID);

  // build new block
  newBlock = blockchain[height-1];

  dbg_write_str("New block sent ");
  printBlock(newBlock);

  // choose peer to send new block for propagation
  uint8_t condensedPeers[UINT8_MAX] = {0};
  uint8_t len = condenseActivePeers(activePeers, condensedPeers);

  // make sure we don't send the block back to whoever we got it from (if we didn't mine it)
  len = removeFromArray(condensedPeers, len, newBlock.minerID);

  if(len > 0)
  {
    uint8_t sentPeer = condensedPeers[trngRandom(len)];
  
    dbg_write_str("Sent peer is ");
    dbg_write_u8(&sentPeer, 1);
    dbg_write_char('\n');

    // send request
    updateFilter(NEW_TX, sentPeer, myID, SHARE, STF0M);
    updateTxBuf(NEW_RX, BROADCAST_ID, sentPeer, NONE, 1, &myID);

    CANSend(NEW_RX);
  }
}


static void rxPulse(uint8_t senderID, uint8_t receiverID, HeaderType header, uint8_t *rxBuf, uint8_t len)
{
  dbg_write_str("Pulse from: ");
  dbg_write_u8(rxBuf, 1);
  dbg_write_char('\n');

  // mark peer as active
  activePeers[rxBuf[0]] = elapsedMS();
}

static void rxDiscover(uint8_t senderID, uint8_t receiverID, HeaderType header, uint8_t *rxBuf, uint8_t len)
{
  // someone is joining the network and they want to find out the longest chain
  if(!doingDiscovery && !chainPartnerID && header == NONE)
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

    if(sendBlock(height, (uint8_t *)&blockchain, *(uint32_t *)rxBuf, CHAIN, myID, chainPartnerID))
    {
      dbg_write_str("Full chain sent\n");
      chainPartnerID = 0;
      resetFilters();
    }
  }
  // receive BLOCK, respond SHARE
  else if(header == BLOCK)
  {
    dbg_write_str("Received discovery block\n");

    // no more data sent, meaning discovery is done
    if(len == 0)
    {
      dbg_write_str("Completed discovery\n");

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
    // clear out blockchain
    Block empty = {0};
    for(uint16_t i=0; i<BLOCKCHAIN_SIZE; i++)
    {
      blockchain[i] = empty;
    }
    height = 0;
    
    chainPartnerID = 0;
    doingDiscovery = false;
    discoverySuccess = false;
  }
}

static void rxNewRx(uint8_t senderID, uint8_t receiverID, HeaderType header, uint8_t *rxBuf, uint8_t len)
{
  if(!doingDiscovery && !newBlockRxPartnerID && header == NONE)
  {
    newBlockRxPartnerID = rxBuf[0];
    blockBytesPos = 0;
    
    dbg_write_str("Received new block transmit request from ");
    dbg_write_u8(&newBlockRxPartnerID, 1);
    dbg_write_char('\n');

    updateFilter(NEW_RX, newBlockRxPartnerID, myID, BLOCK, STF0M);
    updateTxBuf(NEW_TX, myID, newBlockRxPartnerID, SHARE, sizeof(blockBytesPos), (uint8_t *)&blockBytesPos);
    CANSend(NEW_TX);
  }
  else if(header == BLOCK)
  {
    // no more data sent, meaning block was fully received
    if(len == 0)
    {
      dbg_write_str("Completed new block receive, height ");
      dbg_write_u16(&height, 1);
      dbg_write_char('\n');
      printChain();

      blockBytesPos = 0;
      partialBlockPos = 0;
      newBlockRxPartnerID = 0;
      resetFilters();
      
      // propagate the new block
      sendNewestBlock();
    }
    // we successfully receive the data into a partial block buffer, so we can send a request for the next set of bytes
    else if(storePartialBlock(rxBuf, len))
    {
      dbg_write_str("New block bytes stored\n");
      updateTxBuf(NEW_TX, myID, newBlockRxPartnerID, SHARE, sizeof(blockBytesPos), (uint8_t *)&blockBytesPos);
      CANSend(NEW_TX);
    }
    // the block we received didn't validate properly, so just don't accept it
    else
    {
      dbg_write_str("New block error\n");
      newBlockRxPartnerID = 0;
      blockBytesPos = 0;
      partialBlockPos = 0;
      resetFilters();
    }
  }
}

static void rxNewTx(uint8_t senderID, uint8_t receiverID, HeaderType header, uint8_t *rxBuf, uint8_t len)
{
  if(header == SHARE)
  {
    newBlockTxPartnerID = senderID;

    dbg_write_str("Sending partial block to ");
    dbg_write_u8(&senderID, 1);
    dbg_write_char('\n');

    if(sendBlock(1, (uint8_t *)&newBlock, *(uint32_t *)rxBuf, NEW_RX, myID, senderID))
    {
      dbg_write_str("Full block sent\n");

      newBlockTxPartnerID = 0;
      resetFilters();
    }
  }
}

static void canCallback(uint8_t fifoIndex)
{
  if(fifoIndex == 0)
  {
    fifo0Count++;
  }
  else
  {
    fifo1Count++;
  }
}


static void sw0Init()
{
  // set pin PA15 as an input
  // PA15 is the pad for SW0, which is the button on the processor card
  PORT_REGS->GROUP[0].PORT_DIRCLR = PORT_PA15;

  // enable pull-up resistor
  PORT_REGS->GROUP[0].PORT_PINCFG[15] |= PORT_PINCFG_PULLEN_Msk;
  PORT_REGS->GROUP[0].PORT_OUTSET = PORT_PA15;

  // enable input buffer
  PORT_REGS->GROUP[0].PORT_PINCFG[15] |= PORT_PINCFG_INEN_Msk;
}

static void sampleButton()
{
  uint32_t now = elapsedMS();

  // update last state and reset event
  uint8_t lastState = sw0.state;
  sw0.lastEvent = sw0.event;
  sw0.event = 0;
  
  // read and update current state
  uint8_t input = ((PORT_REGS->GROUP[0].PORT_IN & PORT_PA15) == 0);

  // feed hysteresis
  if(input && sw0.accumulator < HYS_ON_MAX)
  {
    sw0.accumulator++;
  }
  else if(!input && sw0.accumulator > HYS_OFF_MIN)
  {
    sw0.accumulator--;
  }

  if(sw0.accumulator >= HYS_ON_LIM)
  {
    sw0.state = 1;
  }
  else if(sw0.accumulator <= HYS_OFF_LIM)
  {
    sw0.state = 0;
  }

  if(sw0.state != lastState)
  {
    if(lastState && !sw0.state)
    {
      sw0.lastRelease = now;
      sw0.pendingInput = 0;

      if(sw0.lastRelease - sw0.lastPress > HOLD_MIN_TIME)
      {
        sw0.event = 2;
      }
      else
      {
        sw0.event = 1;
      }
    }
    else if(!lastState && sw0.state)
    {
      sw0.lastPress = now;
      sw0.pendingInput = 1;
    }
  }
}


static void processTxState(HysObj sw0)
{
  currTxState = txStates[currTxState](sw0);
}

static TxState txIdle(HysObj sw0)
{
  TxState nextState = TX_IDLE;
  static uint8_t count = 0;

  count++;

  if(count * PROCESS_TX_RATE >= BLINK_RATE)
  {
    PORT_REGS->GROUP[0].PORT_OUTTGL = PORT_PA14;
    count = 0;
  }

  if(sw0.pendingInput)
  {
    nextState = TX_READ;
    count = 0;
  }

  return nextState;
}

static TxState txRead(HysObj sw0)
{
  TxState nextState = TX_READ;
  static volatile int msgCount = 0;
  static volatile int letterCount = 0;

  msgCount++;
  letterCount++;

  if(msgCount * SW0_SAMPLE_RATE >= MSG_TIMEOUT || txBufPos >= TRANSACTION_MSG_SIZE)
  {
    msgCount = 0;
    nextState = TX_TRANSMIT;
  }
  else if(letterCount * SW0_SAMPLE_RATE >= LETTER_TIMEOUT || currBinPos >= MORSE_MAX_LEN)
  {
    letterCount = 0;
    currBin[currBinPos] = '\0';
    currBinPos = 0;
    nextState = TX_CONVERT;
  }
  else if(!sw0.pendingInput && !sw0.event)
  {
    PORT_REGS->GROUP[0].PORT_OUTSET = PORT_PA14;
  }
  else
  {
    PORT_REGS->GROUP[0].PORT_OUTCLR = PORT_PA14;
    msgCount = 0;
    letterCount = 0;

    if(!sw0.pendingInput)
    {
      if(sw0.event == 2)
      {
        currBin[currBinPos] = '-';
      }
      else
      {
        currBin[currBinPos] = '.';
      }
      currBinPos++;
    }
  }

  return nextState;
}

static TxState txConvert(HysObj sw0)
{
  TxState nextState = TX_READ;

  char conversion = binToChar(currBin);
  
  if(conversion != '\0')
  {
    txBuf[txBufPos] = conversion;
    txBufPos++;
  }

  currBinPos = 0;

  return nextState;
}

static TxState txTransmit(HysObj sw0)
{
  TxState nextState = TX_IDLE;

  // wait for pending operations
  if(!doingDiscovery && !newBlockTxPartnerID && !newBlockRxPartnerID)
  {
    // create new transaction and block block
    Transaction newTransaction = {.srcID = myID, .msgLen = txBufPos};
    for(int i=0; i<txBufPos; i++)
    {
      newTransaction.msg[i] = txBuf[i];
    }
  
    blockchain[height].minerID = myID;
    blockchain[height].nonce = 3;
    blockchain[height].height = height;
    blockchain[height].transaction = newTransaction;
    icmSHA256((uint8_t *)&blockchain[height-1], sizeof(Block), blockchain[height].prevHash);
    height++;
  
    dbg_write_str("Mined new block: ");
    printBlock(blockchain[height-1]);
  
    // send it out
    sendNewestBlock();
  
    txBufPos = 0;
  }
  // stay in the transmit state to avoid reading new morse code messages
  else
  {    
    nextState = TX_TRANSMIT;
  }

  return nextState;
}


static void processMessage()
{
  CANMessage message = {0};
  bool hasMessage = CANReceive(0, &message);

  if(hasMessage)
  {
    // parse ID into separate fields
    MsgType type = message.id[ID_MSG_TYPE_Pos];
    uint8_t senderID = message.id[ID_SENDER_Pos];
    uint8_t receiverID = message.id[ID_RECEIVER_Pos];
    HeaderType header = message.id[ID_HEADER_Pos] & 0x1F;
  
    static void (*rxTypes[])(uint8_t, uint8_t, HeaderType, uint8_t *, uint8_t) = {rxPulse, rxDiscover, rxChain, rxNewRx, rxNewTx};
  
    rxTypes[type](senderID, receiverID, header, message.data, message.len);
  
    fifo0Count--;
  }
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
      while(!longestChainPeerID && (elapsedMS() - now < DISCOVERY_TIMEOUT))
      {
        processMessage();
      }
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
      while(!chainPartnerID && (elapsedMS() - now < DISCOVERY_TIMEOUT))
      {
        processMessage();
      }
    }

    // wait in this loop while receving the chain from our partner peer.
    while(doingDiscovery)
    {
      processMessage();
    }
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
    blockchain[0].transaction.srcID = 0;
    blockchain[0].transaction.msgLen = 0;
    height++;

    dbg_write_str("Block added ");
    printBlock(blockchain[height-1]);
  }
  else
  {
    doingDiscovery = true;
    chainPartnerID = 0;
    discoverySuccess = false;
  }
  
  sw0Init();
  icmInit();
  CANInit(rxFifo0Start, rxFifo1Start, txBufStart, extendedFilterStart, RX_FIFO_ELEMENT_COUNT, RX_FIFO_ELEMENT_COUNT, TX_BUF_ELEMENT_COUNT, EXTENDED_FILTER_COUNT, canCallback);
  heartInit();
  trngInit();
  spiInit();
  displayInit();

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
  uint32_t pulseTimestamp = 0;
  uint32_t peerCheckTimestamp = PEER_CHECK_RATE;
  uint32_t sw0SampleTimestamp = 0;
  uint32_t processTxTimestamp = 0;

  for(;;)
  {
    uint32_t msCount = elapsedMS();

    // process messages until there are no more to be processed
    while(fifo0Count > 0)
    {
      processMessage();
    }

    // while(fifo1Count > 0)
    // {
    //   printRejected();
    // }

    if(msCount >= sw0SampleTimestamp)
    {
      sampleButton();
      sw0SampleTimestamp = msCount + SW0_SAMPLE_RATE;
    }
    if(msCount >= processTxTimestamp)
    {
      processTxState(sw0);
      processTxTimestamp = msCount + PROCESS_TX_RATE;
    }
    if(msCount >= pulseTimestamp)
    {
      CANSend(PULSE);
      pulseTimestamp = msCount + PULSE_RATE;
    }
    if(msCount >= peerCheckTimestamp)
    {
      peerCheck(activePeers);
      peerCheckTimestamp = msCount + PEER_CHECK_RATE;
    }
  }

  return 0;
}
