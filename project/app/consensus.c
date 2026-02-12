#include "app_util.h"
#include "net.h"
#include "can.h"
#include "heart.h"

#define CONSENSUS_PROC_TIME 100 // ms
#define CONSENSUS_RESEND_TIMEOUT 1000 // ms

typedef enum CONSENSUS_STATE
{
  IDLE,
  REQUEST,
  PARTNER_ACK,
  BLOCK_ACK,
  DONE,
  NUM_CONSENSUS_STATES
} ConsensusState;

static ConsensusState consensusIdle();
static ConsensusState consensusRequest();
static ConsensusState consensusPartnerAck();
static ConsensusState consensusBlockAck();
static ConsensusState consensusDone();

ConsensusState processConsensus();

static ConsensusState (*states[])() = {consensusIdle, consensusRequest, consensusPartnerAck, consensusBlockAck, consensusDone};
static ConsensusState currState = IDLE;

static bool doingConsensus = false;
static bool requestingConsensus = false;

static uint64_t blockBytesPos = 0;


ConsensusState processConsensus()
{
  currState = states[currState]();
}


static ConsensusState consensusIdle(MsgType type, uint8_t senderID, uint8_t receiverID, HeaderType header, uint8_t *rxBuf, uint8_t len, uint8_t myID, bool waiting)
{
  ConsensusState next = IDLE;

  if(waiting)
  {
    requestingConsensus = true;
    next = REQUEST;
  }
  else if(type == CONSENSUS && header == NONE)
  {
    uint8_t consensusReqID = rxBuf[0];

    // set this flag to temporarily prevent acceptance of other consensus requests
    doingConsensus = true;

    dbg_write_str("Sharing blocks with ");
    dbg_write_u8(&consensusReqID, 1);
    dbg_write_char('\n');
  
    // update consensus filter with partner ID to reject all other consensus requests
    updateFilter(CONSENSUS, consensusReqID, myID, ACK, STF0M);
    
    // setup consensus buffer to send confirmation to src peer
    updateTxBuf(CONSENSUS, BROADCAST_ID, consensusReqID, ACK, 1, &myID);
    CANSend(CONSENSUS);

    next = PARTNER_ACK;
  }

  return next;
}

static ConsensusState consensusRequest(MsgType type, uint8_t senderID, uint8_t receiverID, HeaderType header, uint8_t *rxBuf, uint8_t len, uint8_t myID, bool waiting)
{
  static uint32_t lastTime = 0;
  static uint32_t now = 0;
  lastTime = now;
  now = elapsedMS();

  ConsensusState next = REQUEST;

  if(requestingConsensus)
  {
    if(now - lastTime >= CONSENSUS_RESEND_TIMEOUT)
    {
      CANSend(CONSENSUS);
    }
  }
  else if(type == CONSENSUS && header == ACK)
  {
    dbg_write_str("Consensus handshake completed\n");

    doingConsensus = true;
    requestingConsensus = false;
    
    // setup filter to only accept consensus blocks from partner
    uint8_t partnerID = rxBuf[0];
    updateFilter(BLOCK, partnerID, myID, SHARE, STF0M);
    
    // send request for first block
    blockBytesPos = 0;
    updateTxBuf(CONSENSUS, myID, partnerID, ACK, sizeof(blockBytesPos), (uint8_t *)&blockBytesPos);
    CANSend(CONSENSUS);

    next = BLOCK_ACK;
  }
  else
  {

  }

  return next;
}
