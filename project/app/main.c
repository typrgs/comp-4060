#include "heart.h"
#include "can.h"
#include "icm.h"
#include "net.h"
#include "blockchain.h"
#include "trng.h"
#include "morse_map.h"
#include "display.h"
#include "spi.h"

#define BLOCKCHAIN_DIFFICULTY 5

#define EXTENDED_FILTER_COUNT NUM_MSG_TYPES
#define RX_FIFO_ELEMENT_COUNT 20
#define TX_BUF_ELEMENT_COUNT NUM_MSG_TYPES

#define PULSE_RATE 5000                // ms
#define PEER_CHECK_RATE PULSE_RATE * 2 // ms
#define BLINK_RATE 500                 // ms
#define DISPLAY_REFRESH_RATE 500       // ms

#define DISCOVERY_TIMEOUT 1000  // ms
#define CHAIN_TIMEOUT 2000      // ms
#define NEW_RECV_TIMEOUT 1000   // ms
#define REORGANISE_TIMEOUT 1000 // ms

#define BLOCK_SEND_DELAY 25 // ms
#define PROP_DELAY 50       // ms

#define HYS_ON_MAX 1
#define HYS_ON_LIM 1
#define HYS_OFF_MIN 0
#define HYS_OFF_LIM 0

#define SW0_SAMPLE_RATE 10 // ms
#define PROCESS_SM_RATE 10 // ms

#define HOLD_MIN_TIME 250  // ms
#define MSG_TIMEOUT 1000   // ms
#define LETTER_TIMEOUT 400 // ms

// setup message RAM for CAN
#define EXTENDED_FILTER_SIZE EXTENDED_FILTER_COUNT *EXTENDED_FILTER_WORDS
#define RX_FIFO_SIZE RX_FIFO_ELEMENT_COUNT *RX_FIFO_ELEMENT_WORDS
#define TX_BUF_SIZE TX_BUF_ELEMENT_COUNT *TX_BUF_ELEMENT_WORDS

static uint32_t messageRAM[EXTENDED_FILTER_SIZE + RX_FIFO_SIZE + RX_FIFO_SIZE + TX_BUF_SIZE] __ALIGNED(32);

// setup start locations of necessary structures
static uint32_t *extendedFilterStart = (uint32_t *)&(messageRAM[0]);
static uint32_t *rxFifo0Start = (uint32_t *)&(messageRAM[EXTENDED_FILTER_SIZE]);
static uint32_t *rxFifo1Start = (uint32_t *)&(messageRAM[EXTENDED_FILTER_SIZE + RX_FIFO_SIZE]);
static uint32_t *txBufStart = (uint32_t *)&(messageRAM[EXTENDED_FILTER_SIZE + RX_FIFO_SIZE + RX_FIFO_SIZE]);

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

typedef enum RX_STATE
{
  RX_ENTRY,
  RX_DISCOVER_SEND,
  RX_DISCOVER_RECV,
  RX_CHAIN,
  RX_NEW_RECV,
  RX_REORGANISE,
  RX_NUM_STATES
} RxState;

typedef enum TX_STATE
{
  TX_IDLE,
  TX_READ,
  TX_CONVERT,
  TX_TRANSMIT,
  TX_NUM_STATES
} TxState;

typedef enum PROP_STATE
{
  PROP_IDLE,
  PROP_SEND,
  NUM_PROP_STATES
} PropState;

static RxState rxEntry(bool hasMessage, MsgType type, uint8_t senderID, uint8_t receiverID, HeaderType header, uint8_t *rxBuf, uint8_t len);
static RxState rxDiscoverSend(bool hasMessage, MsgType type, uint8_t senderID, uint8_t receiverID, HeaderType header, uint8_t *rxBuf, uint8_t len);
static RxState rxDiscoverRecv(bool hasMessage, MsgType type, uint8_t senderID, uint8_t receiverID, HeaderType header, uint8_t *rxBuf, uint8_t len);
static RxState rxChain(bool hasMessage, MsgType type, uint8_t senderID, uint8_t receiverID, HeaderType header, uint8_t *rxBuf, uint8_t len);
static RxState rxNewRecv(bool hasMessage, MsgType type, uint8_t senderID, uint8_t receiverID, HeaderType header, uint8_t *rxBuf, uint8_t len);
static RxState rxReorganise(bool hasMessage, MsgType type, uint8_t senderID, uint8_t receiverID, HeaderType header, uint8_t *rxBuf, uint8_t len);

static RxState (*rxStates[])(bool, MsgType, uint8_t, uint8_t, HeaderType, uint8_t *, uint8_t) = {rxEntry, rxDiscoverSend, rxDiscoverRecv, rxChain, rxNewRecv, rxReorganise};
static RxState currRxState = RX_DISCOVER_SEND;

static TxState txIdle(HysObj);
static TxState txRead(HysObj);
static TxState txConvert(HysObj);
static TxState txTransmit(HysObj);

static TxState (*txStates[])(HysObj) = {txIdle, txRead, txConvert, txTransmit};
static TxState currTxState = TX_IDLE;

static PropState propIdle(Block newBlock);
static PropState propSend(Block newBlock);

static PropState (*propStates[])(Block) = {propIdle, propSend};
static PropState currPropState = PROP_IDLE;

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
static bool displayAvailable = false;

static uint8_t myID;
static uint32_t sigKey = 0;
static uint32_t activePeers[UINT8_MAX] = {0};

static Block blockchain[BLOCKCHAIN_SIZE] = {0};
static uint16_t height = 0;
static uint8_t longestChainPeerID = 0;
static uint16_t longestChainHeight = 0;

static bool discoverySuccess = false;
static uint8_t chainPartnerID = 0;

static uint8_t partialBlock[sizeof(Block)];
static uint8_t partialBlockPos = 0;

static Block newBlock = {0};
static uint8_t newBlockSenderID = 0;
static uint8_t condensedPeers[UINT8_MAX] = {0};
static uint8_t condensedPeersLen = 0;

static void readParams()
{
  // save device ID given in flash parameters
  uint8_t *params = (uint8_t *)0x000fe000;
  myID = params[0];

  // this parameter indicates whether this node should define the genesis block on startup
  if (params[1])
  {
    startNode = true;
  }
  if (params[2])
  {
    displayAvailable = true;
  }

  sigKey = *(uint32_t *)(&params[3]);
}

static void updateTxBuf(MsgType type, uint8_t senderID, uint8_t receiverID, uint8_t header, uint8_t dataLength, uint8_t *data)
{
  txBufs[type].bufIndex = type;
  txBufs[type].id[ID_MSG_TYPE_Pos] = type;
  txBufs[type].id[ID_SENDER_Pos] = senderID;
  txBufs[type].id[ID_RECEIVER_Pos] = receiverID;
  txBufs[type].id[ID_HEADER_Pos] = header;
  txBufs[type].dataLength = dataLength;
  if (data != NULL)
  {
    for (int i = 0; i < dataLength; i++)
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
  for (uint8_t i = 0; i < NUM_MSG_TYPES; i++)
  {
    updateFilter(i, BROADCAST_ID, BROADCAST_ID, HDR_NONE, STF0M);
  }

  updateFilter(MSG_NEW, BROADCAST_ID, myID, HDR_NONE, STF0M);
  updateFilter(MSG_REORGANISE, BROADCAST_ID, myID, HDR_NONE, STF0M);
}

static void resetTxBufs()
{
  for (uint8_t i = 0; i < NUM_MSG_TYPES; i++)
  {
    updateTxBuf(i, myID, BROADCAST_ID, 0, 0, NULL);
  }

  updateTxBuf(MSG_PULSE, BROADCAST_ID, BROADCAST_ID, HDR_NONE, 1, &myID);
}

static void resetBlockchain()
{
  Block empty = {0};

  for (uint16_t i = 0; i < height; i++)
  {
    blockchain[i] = empty;
  }
  height = 0;
}

static void printBlock(Block block)
{
  dbg_write_u8(&block.transaction.srcID, 1);
  dbg_write_u8(&block.transaction.msgLen, 1);
  dbg_write_u8((uint8_t *)&block.transaction.msg, block.transaction.msgLen);
  dbg_write_u32(&block.nonce, 1);
  dbg_write_u8(block.prevHash, 3);
  dbg_write_u8(block.transaction.signature, 3);
  dbg_write_char('\n');
}

static void printChain()
{
  for (int i = 0; i < height; i++)
  {
    printBlock(blockchain[i]);
  }
}

static BlockError storePartialBlock(uint8_t *rxBuf, uint8_t len, uint16_t chainIndex)
{
  BlockError result = BLOCK_ERR_INCOMPLETE;

  // build block from received data
  for (int i = 0; i < len; i++)
  {
    partialBlock[partialBlockPos++] = rxBuf[i];

    // check if a full block has been constructed
    if (partialBlockPos == sizeof(Block))
    {
      // add block to chain if verification returns true
      Block tempBlock = *((Block *)partialBlock);
      dbg_write_str("Full block constructed ");
      printBlock(tempBlock);
      result = verifyBlock(blockchain, height, (uint8_t *)&sigKey, sizeof(sigKey), tempBlock);

      if (result == BLOCK_ERR_VALID)
      {
        dbg_write_str("New block added ");
        printBlock(tempBlock);

        blockchain[chainIndex] = tempBlock;
      }

      // reset partial block buffer
      for (int i = 0; i < sizeof(Block); i++)
      {
        partialBlock[i] = 0;
      }
      partialBlockPos = 0;
    }
  }

  return result;
}

static void sendBlocks(uint16_t height, uint8_t *blockBytes, HeaderType header, uint8_t senderID, uint8_t receiverID)
{
  uint32_t bytesPos = 0;

  // calculate how many bytes are left until the end of the chain
  uint32_t bytesUntilEnd = (height * sizeof(Block)) - bytesPos;

  while (bytesUntilEnd > 0)
  {
    // if there is more data, send out bytes
    // bytes sent is either the full message size, or whatever might be remaining
    uint32_t bytesSent = (bytesUntilEnd > CAN_MESSAGE_SIZE ? CAN_MESSAGE_SIZE : bytesUntilEnd);

    updateTxBuf(MSG_BLOCK, senderID, receiverID, header, bytesSent, &blockBytes[bytesPos]);
    CANSend(MSG_BLOCK);

    bytesPos += bytesSent;

    // add small delay to prevent message loss while sending many blocks in a row
    uint32_t now = elapsedMS();
    while (elapsedMS() - now < BLOCK_SEND_DELAY)
      ;

    bytesUntilEnd = (height * sizeof(Block)) - bytesPos;
  }

  updateTxBuf(MSG_BLOCK, senderID, receiverID, header, 0, NULL);
  CANSend(MSG_BLOCK);
}

static void   sendNewestBlock(uint8_t senderID)
{
  // build new block
  newBlock = blockchain[height - 1];

  dbg_write_str("Sending new block: ");
  printBlock(newBlock);

  newBlockSenderID = senderID;
}

static uint8_t condenseActivePeers(uint32_t *arr, uint8_t *condensedArr)
{
  uint8_t len = 0;

  // squish non-zero arr contents into the start of condensedArr
  for (uint8_t i = 0; i < UINT8_MAX; i++)
  {
    if (activePeers[i])
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

  for (uint8_t i = 0; i < size && pos < 0; i++)
  {
    if (arr[i] == element)
      pos = i;
  }

  if (pos >= 0)
  {
    for (uint8_t i = pos; i < size - 1; i++)
    {
      arr[i] = arr[i + 1];
      arr[i + 1] = 0;
    }

    newSize--;
  }

  return newSize;
}

static void peerCheck(uint32_t *activePeers)
{
  uint32_t now = elapsedMS();

  for (uint8_t i = 0; i < UINT8_MAX; i++)
  {
    if (activePeers[i])
    {
      // check if each peer has recently pulsed
      if (now - activePeers[i] > PULSE_RATE)
      {
        activePeers[i] = 0;
      }
    }
  }
}

static RxState rxEntry(bool hasMessage, MsgType type, uint8_t senderID, uint8_t receiverID, HeaderType header, uint8_t *rxBuf, uint8_t len)
{
  RxState nextState = RX_ENTRY;

  if (hasMessage)
  {
    if (type == MSG_PULSE)
    {
      dbg_write_str("Pulse from ");
      dbg_write_u8(rxBuf, 1);
      dbg_write_char('\n');

      activePeers[rxBuf[0]] = elapsedMS();
    }
    else if (type == MSG_DISCOVER && header == HDR_NONE)
    {
      nextState = rxDiscoverRecv(hasMessage, type, senderID, receiverID, header, rxBuf, len);
    }
    // we are either receiving a chain or sending our chain, so change state to block other requests
    else if (type == MSG_CHAIN && header == HDR_NONE)
    {
      nextState = rxChain(hasMessage, type, senderID, receiverID, header, rxBuf, len);
    }
    else if (type == MSG_NEW && header == HDR_NONE)
    {
      nextState = rxNewRecv(hasMessage, type, senderID, receiverID, header, rxBuf, len);
    }
  }

  return nextState;
}

static RxState rxDiscoverSend(bool hasMessage, MsgType type, uint8_t senderID, uint8_t receiverID, HeaderType header, uint8_t *rxBuf, uint8_t len)
{
  RxState nextState = RX_DISCOVER_RECV;

  dbg_write_str("Broadcasting discovery request\n");
  updateFilter(MSG_DISCOVER, BROADCAST_ID, myID, HDR_CHAIN, STF0M);
  updateTxBuf(MSG_DISCOVER, BROADCAST_ID, BROADCAST_ID, HDR_NONE, 1, &myID);
  CANSend(MSG_DISCOVER);

  return nextState;
}

static RxState rxDiscoverRecv(bool hasMessage, MsgType type, uint8_t senderID, uint8_t receiverID, HeaderType header, uint8_t *rxBuf, uint8_t len)
{
  RxState nextState = RX_DISCOVER_RECV;
  static uint8_t count = 0;

  count++;

  if (count * PROCESS_SM_RATE >= DISCOVERY_TIMEOUT)
  {
    count = 0;

    if (longestChainPeerID)
    {
      nextState = RX_CHAIN;

      dbg_write_str("Discover partner data: ");
      dbg_write_u16(&longestChainHeight, 1);
      dbg_write_u8(&longestChainPeerID, 1);
      dbg_write_char('\n');

      // ask last peer that responded with the longest chain for their blockchain
      dbg_write_str("Sending chain request\n");
      updateFilter(MSG_BLOCK, longestChainPeerID, myID, HDR_CHAIN, STF0M);
      updateTxBuf(MSG_CHAIN, BROADCAST_ID, longestChainPeerID, HDR_NONE, 1, &myID);
      CANSend(MSG_CHAIN);
    }
    else
    {
      nextState = RX_DISCOVER_SEND;
    }
  }
  else if (hasMessage && type == MSG_DISCOVER)
  {
    if (header == HDR_NONE)
    {
      dbg_write_str("Received discovery request\n");

      // place ID and height into data buffer
      uint8_t data[sizeof(height) + sizeof(myID)];
      data[0] = myID;
      data[1] = ((uint8_t *)&height)[0];
      data[2] = ((uint8_t *)&height)[1];

      updateFilter(MSG_CHAIN, BROADCAST_ID, myID, HDR_NONE, STF0M);
      updateTxBuf(MSG_DISCOVER, BROADCAST_ID, rxBuf[0], HDR_CHAIN, sizeof(data), data);
      CANSend(MSG_DISCOVER);

      nextState = RX_CHAIN;
    }
    else if (header == HDR_CHAIN)
    {
      dbg_write_str("Received discovery data\n");
      // extract data from buffer
      uint8_t peer = rxBuf[0];
      uint16_t receivedHeight = *(uint16_t *)(&rxBuf[1]);

      // save the received height if larger than the currently saved value
      if (receivedHeight >= longestChainHeight)
      {
        longestChainHeight = receivedHeight;
        longestChainPeerID = peer;
      }
    }
  }

  return nextState;
}

static void resetChainState()
{
  chainPartnerID = 0;
  longestChainPeerID = 0;
  longestChainHeight = 0;
  partialBlockPos = 0;
  resetFilters();
}

static RxState rxChain(bool hasMessage, MsgType type, uint8_t senderID, uint8_t receiverID, HeaderType header, uint8_t *rxBuf, uint8_t len)
{
  RxState nextState = RX_CHAIN;
  static uint8_t count = 0;

  count++;

  if (count * PROCESS_SM_RATE >= CHAIN_TIMEOUT)
  {
    dbg_write_str("Exiting chain state on timeout\n");
    count = 0;

    resetChainState();

    if (!discoverySuccess)
    {
      nextState = RX_DISCOVER_SEND;
    }
    else
    {
      nextState = RX_ENTRY;
    }
  }
  else if (hasMessage)
  {
    count = 0;

    if (type == MSG_CHAIN && header == HDR_NONE)
    {
      dbg_write_str("Received chain request, sending blockchain\n");

      // save partner ID, get filter ready for chain sharing requests, send chain ack
      chainPartnerID = rxBuf[0];

      // send entire blockchain
      sendBlocks(height, (uint8_t *)&blockchain, HDR_CHAIN, myID, chainPartnerID);

      nextState = RX_ENTRY;

      dbg_write_str("Full chain sent\n");
      resetChainState();
    }
    else if (type == MSG_BLOCK && header == HDR_CHAIN)
    {
      dbg_write_str("Received partial chain block\n");

      if (len == 0)
      {
        dbg_write_str("Completed discovery\n");

        nextState = RX_ENTRY;

        resetChainState();
        discoverySuccess = true;
      }
      else
      {
        BlockError storageResult = storePartialBlock(rxBuf, len, height);
        if (storageResult == BLOCK_ERR_VALID)
        {
          height++;
        }
        else if (storageResult != BLOCK_ERR_INCOMPLETE)
        {
          dbg_write_str("Invalid chain\n");

          nextState = RX_DISCOVER_SEND;

          resetChainState();
          discoverySuccess = false;
        }
      }
    }
  }

  return nextState;
}

static void resetNewRecvState()
{
  partialBlockPos = 0;
  resetFilters();
}

static RxState handleNewBlockError(BlockError err, uint8_t senderID)
{
  RxState nextState = RX_ENTRY;
  dbg_write_str("New received block is invalid: ");
  dbg_write_u8(&err, 1);
  dbg_write_char('\n');

  resetNewRecvState();

  // if the received block is taller than expected, we need to reorganise our chain to include the blocks on the longer chain
  if (err == BLOCK_ERR_TALLER)
  {
    // send a request to the block sender and request all blocks in their chain starting from our current height
    updateFilter(MSG_REORGANISE, senderID, myID, HDR_REORGANISE, STF0M);
    updateTxBuf(MSG_REORGANISE, BROADCAST_ID, senderID, HDR_NONE, 2, (uint8_t *)&height);
    CANSend(MSG_REORGANISE);

    nextState = RX_REORGANISE;
    discoverySuccess = false;
  }

  return nextState;
}

static RxState rxNewRecv(bool hasMessage, MsgType type, uint8_t senderID, uint8_t receiverID, HeaderType header, uint8_t *rxBuf, uint8_t len)
{
  RxState nextState = RX_NEW_RECV;
  static uint8_t count = 0;

  count++;

  if (count * PROCESS_SM_RATE >= NEW_RECV_TIMEOUT)
  {
    count = 0;

    dbg_write_str("Timed out receiving new block\n");
    resetNewRecvState();
    nextState = RX_ENTRY;
  }
  else if (hasMessage)
  {
    count = 0;

    if (type == MSG_NEW && header == HDR_NONE)
    {
      dbg_write_str("Ready to begin receiving new block\n");
      dbg_write_u8(rxBuf, 1);
      dbg_write_char('\n');
      updateFilter(MSG_BLOCK, rxBuf[0], myID, HDR_NEW, STF0M);
    }
    else if (type == MSG_BLOCK && header == HDR_NEW && len > 0)
    {
      dbg_write_str("Received partial new block\n");
      BlockError storageResult = storePartialBlock(rxBuf, len, height);

      if (storageResult == BLOCK_ERR_VALID)
      {
        height++;
        dbg_write_str("Completed new block receive, height ");
        dbg_write_u16(&height, 1);
        dbg_write_char('\n');
        printChain();

        nextState = RX_ENTRY;

        resetNewRecvState();

        // propagate it
        sendNewestBlock(senderID);
      }
      else if (storageResult != BLOCK_ERR_INCOMPLETE)
      {
        nextState = handleNewBlockError(storageResult, senderID);
      }
    }
  }

  return nextState;
}

static RxState rxReorganise(bool hasMessage, MsgType type, uint8_t senderID, uint8_t receiverID, HeaderType header, uint8_t *rxBuf, uint8_t len)
{
  RxState nextState = RX_REORGANISE;
  static uint8_t count = 0;

  count++;

  if (count * PROCESS_SM_RATE >= REORGANISE_TIMEOUT)
  {
    count = 0;
    resetFilters();
    resetBlockchain();
    nextState = RX_DISCOVER_SEND;
  }
  else if (hasMessage)
  {
    count = 0;

    if (type == MSG_REORGANISE && header == HDR_NONE)
    {
      uint16_t startingHeight = *(uint16_t *)rxBuf;
      sendBlocks(height - startingHeight, (uint8_t *)&blockchain[startingHeight], HDR_REORGANISE, myID, senderID);
      nextState = RX_ENTRY;
    }
    else if (type == MSG_BLOCK && header == HDR_REORGANISE)
    {
      if (len == 0)
      {
        discoverySuccess = true;
        resetFilters();
        nextState = RX_ENTRY;
      }
      else
      {
        BlockError storageResult = storePartialBlock(rxBuf, len, height);

        if (storageResult == BLOCK_ERR_VALID)
        {
          height++;
        }
        // if we encounter any error while receiving intermediate blocks, we reset our chain and do a rediscovery
        else if (storageResult != BLOCK_ERR_INCOMPLETE)
        {
          resetFilters();
          resetBlockchain();
          nextState = RX_DISCOVER_SEND;
        }
      }
    }
  }

  return nextState;
}

static void canCallback(uint8_t fifoIndex)
{
  if (fifoIndex == 0)
  {
    fifo0Count++;
  }
  else
  {
    fifo1Count++;
  }
}

static void processRxState()
{
  CANMessage message = {0};
  bool hasMessage = CANReceive(0, &message);

  // parse ID into separate fields
  MsgType type = message.id[ID_MSG_TYPE_Pos];
  uint8_t senderID = message.id[ID_SENDER_Pos];
  uint8_t receiverID = message.id[ID_RECEIVER_Pos];
  HeaderType header = message.id[ID_HEADER_Pos] & 0x1F;

  currRxState = rxStates[currRxState](hasMessage, type, senderID, receiverID, header, message.data, message.len);

  if (hasMessage)
  {
    fifo0Count--;
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
  if (input && sw0.accumulator < HYS_ON_MAX)
  {
    sw0.accumulator++;
  }
  else if (!input && sw0.accumulator > HYS_OFF_MIN)
  {
    sw0.accumulator--;
  }

  if (sw0.accumulator >= HYS_ON_LIM)
  {
    sw0.state = 1;
  }
  else if (sw0.accumulator <= HYS_OFF_LIM)
  {
    sw0.state = 0;
  }

  if (sw0.state != lastState)
  {
    if (lastState && !sw0.state)
    {
      sw0.lastRelease = now;
      sw0.pendingInput = 0;

      if (sw0.lastRelease - sw0.lastPress > HOLD_MIN_TIME)
      {
        sw0.event = 2;
      }
      else
      {
        sw0.event = 1;
      }
    }
    else if (!lastState && sw0.state)
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

  if (count * PROCESS_SM_RATE >= BLINK_RATE)
  {
    PORT_REGS->GROUP[0].PORT_OUTTGL = PORT_PA14;
    count = 0;
  }

  if (sw0.pendingInput)
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

  if (msgCount * SW0_SAMPLE_RATE >= MSG_TIMEOUT || txBufPos >= TRANSACTION_MSG_SIZE)
  {
    msgCount = 0;
    nextState = TX_TRANSMIT;
  }
  else if (letterCount * SW0_SAMPLE_RATE >= LETTER_TIMEOUT || currBinPos >= MORSE_MAX_LEN)
  {
    letterCount = 0;
    currBin[currBinPos] = '\0';
    currBinPos = 0;
    nextState = TX_CONVERT;
  }
  else if (!sw0.pendingInput && !sw0.event)
  {
    PORT_REGS->GROUP[0].PORT_OUTSET = PORT_PA14;
  }
  else
  {
    PORT_REGS->GROUP[0].PORT_OUTCLR = PORT_PA14;
    msgCount = 0;
    letterCount = 0;

    if (!sw0.pendingInput)
    {
      if (sw0.event == 2)
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

  if (conversion != '\0')
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
  if (!newBlockSenderID)
  {
    // create new transaction and block block
    Transaction newTransaction = {.srcID = myID, .msgLen = txBufPos};
    for (int i = 0; i < txBufPos; i++)
    {
      newTransaction.msg[i] = txBuf[i];
    }
    signTransaction(&newTransaction, (uint8_t *)&sigKey, sizeof(sigKey));

    blockchain[height].minerID = myID;

    blockchain[height].nonce = trngRandom(0);
    while (!verifyNonce(blockchain[height].nonce))
      blockchain[height].nonce = trngRandom(0);

    blockchain[height].height = height;
    blockchain[height].transaction = newTransaction;
    icmSHA256((uint8_t *)&blockchain[height - 1], sizeof(Block), blockchain[height].prevHash);
    height++;

    dbg_write_str("Mined new block: ");
    printBlock(blockchain[height - 1]);

    // send it out
    sendNewestBlock(myID);

    txBufPos = 0;
  }
  // stay in the transmit state to avoid reading new morse code messages
  else
  {
    nextState = TX_TRANSMIT;
  }

  return nextState;
}

static PropState propIdle(Block newBlock)
{
  PropState nextState = PROP_IDLE;

  if (newBlockSenderID)
  {
    nextState = PROP_SEND;

    // setup condensedPeers array
    condensedPeersLen = condenseActivePeers(activePeers, condensedPeers);

    // remove the miner of the block so they aren't resent it
    condensedPeersLen = removeFromArray(condensedPeers, condensedPeersLen, newBlock.minerID);

    // remove whoever sent us the block so they aren't resent it
    if (newBlockSenderID != myID)
      condensedPeersLen = removeFromArray(condensedPeers, condensedPeersLen, newBlockSenderID);
  }

  return nextState;
}

static PropState propSend(Block newBlock)
{
  PropState nextState = PROP_SEND;
  static uint8_t count = 0;

  count++;

  if (count >= PEER_PROPAGATION_COUNT || !newBlockSenderID || condensedPeersLen == 0)
  {
    count = 0;
    nextState = PROP_IDLE;

    newBlockSenderID = 0;
    condensedPeersLen = 0;

    dbg_write_str("New block propagated\n");
  }
  else if (newBlockSenderID && condensedPeersLen > 0)
  {
    uint8_t sentPeer = condensedPeers[trngRandom(condensedPeersLen)];

    dbg_write_str("Sending to ");
    dbg_write_u8(&sentPeer, 1);
    dbg_write_char('\n');

    // send ready check to peer
    updateTxBuf(MSG_NEW, BROADCAST_ID, sentPeer, HDR_NONE, 1, &myID);
    CANSend(MSG_NEW);

    // wait a bit before sending block bytes
    uint32_t now = elapsedMS();
    while (elapsedMS() - now < PROP_DELAY)
      ;

    // send new block to peer
    sendBlocks(1, (uint8_t *)&newBlock, HDR_NEW, myID, sentPeer);

    // remove peer from array to avoid resending on the next state processing call
    condensedPeersLen = removeFromArray(condensedPeers, condensedPeersLen, sentPeer);
  }

  return nextState;
}

static void processPropState(Block newBlock)
{
  currPropState = propStates[currPropState](newBlock);
}

static uint8_t countDigits(uint16_t number)
{
  uint8_t count = 1;

  while (number > 0)
  {
    number /= 10;
    count++;
  }

  return count;
}

static int drawNumber(uint16_t number, uint16_t colour, uint8_t row, uint8_t startCol)
{
  uint8_t len = countDigits(number);
  uint8_t stack = 0;

  for (uint8_t i = 0; i < len; i++)
  {
    stack *= 10;
    stack += number % 10;
    number /= 10;
  }

  for (uint16_t i = startCol; i < startCol + len; i++)
  {
    displayDrawFont(row, i, colour, stack % 10);
    stack /= 10;
  }

  return len;
}

static void updateDisplay(uint16_t fontColour)
{
  static uint8_t lastHeight = 0;

  if (height > lastHeight)
  {
    lastHeight = height;

    displayWipe(BLACK);

    int gridRow = 0;
    int gridCol = 0;
    for (int i = height - 1; i >= 0 && gridRow < DISPLAY_GRID_MAX_ROW; i--)
    {
      Block toPrint = blockchain[i];

      gridCol += drawNumber(toPrint.transaction.srcID, fontColour, gridRow, gridCol);
      displayDrawFont(gridRow, gridCol++, fontColour, DISPLAY_COLON_INDEX);

      for (int j = 0; j < toPrint.transaction.msgLen && gridRow < DISPLAY_GRID_MAX_ROW; j++)
      {
        displayDrawFont(gridRow, gridCol++, fontColour, toPrint.transaction.msg[j]);

        if (gridCol >= DISPLAY_GRID_MAX_COL)
        {
          gridRow++;
          gridCol = 0;
        }
      }

      if (gridCol != 0)
      {
        gridRow++;
        gridCol = 0;
      }
    }
  }
}

static void startup()
{
  readParams();

  // start node initializes blockchain on startup, all others do consensus on startup
  if (startNode)
  {
    chainPartnerID = 0;
    discoverySuccess = true;

    blockchain[0].nonce = GENSIS_NONCE;
    blockchain[0].transaction.srcID = GENESIS_SRC_ID;
    blockchain[0].transaction.msgLen = GENESIS_MSG_LEN;
    for (int i = 0; i < GENESIS_MSG_LEN; i++)
    {
      blockchain[0].transaction.msg[i] = GENESIS_MSG[i];
    }
    blockchain[0].transaction.msg[GENESIS_MSG_LEN] = '\0';
    height++;

    dbg_write_str("Block added ");
    printBlock(blockchain[height - 1]);
  }
  else
  {
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
  blockchainInit(BLOCKCHAIN_DIFFICULTY, HMACSign, HMACVerify, icmSHA256);

  // setup filters
  resetFilters();

  // setup transmit buffers
  resetTxBufs();

  // LED output
  PORT_REGS->GROUP[0].PORT_DIRSET = PORT_PA14;
  PORT_REGS->GROUP[0].PORT_OUTSET = PORT_PA14;

  if (!startNode)
  {
    currRxState = RX_DISCOVER_SEND;
  }
  else
  {
    currRxState = RX_ENTRY;
  }
}

int main()
{
#ifndef NDEBUG
  for (int i = 0; i < 1000000; i++)
    ;
#endif

  // enable interrupts
  __enable_irq();

  startup();

  // stop program if essential params are not set properly
  if (!myID || !sigKey)
    return -1;

  // timestamps for event scheduling
  uint32_t pulseTimestamp = 0;
  uint32_t peerCheckTimestamp = PEER_CHECK_RATE;
  uint32_t sw0SampleTimestamp = 0;
  uint32_t processSmTimestamp = 0;
  uint32_t displayTimestamp = 0;

  for (;;)
  {
    uint32_t msCount = elapsedMS();

    if (msCount >= sw0SampleTimestamp)
    {
      sampleButton();
      sw0SampleTimestamp = msCount + SW0_SAMPLE_RATE;
    }
    if (msCount >= pulseTimestamp)
    {
      CANSend(MSG_PULSE);
      pulseTimestamp = msCount + PULSE_RATE;
    }
    if (msCount >= peerCheckTimestamp)
    {
      peerCheck(activePeers);
      peerCheckTimestamp = msCount + PEER_CHECK_RATE;
    }
    if (displayAvailable && msCount >= displayTimestamp)
    {
      updateDisplay(BLUE);
      displayTimestamp = msCount + DISPLAY_REFRESH_RATE;
    }
    if (msCount >= processSmTimestamp)
    {
      processRxState();

      if (discoverySuccess)
      {
        processTxState(sw0);
        processPropState(newBlock);
      }

      processSmTimestamp = msCount + PROCESS_SM_RATE;
    }
  }

  return 0;
}
