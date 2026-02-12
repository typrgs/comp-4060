#include "app_util.h"

static CANTxBuf *txBufs = NULL;
static CANExtFilter *filters = NULL;

void setTxBufs(CANTxBuf *passedTxBufs)
{
  txBufs = passedTxBufs;
}

void setFilters(CANExtFilter *passedFilters)
{
  filters = passedFilters;
}

void updateTxBuf(MsgType type, uint8_t senderID, uint8_t receiverID, uint8_t header, uint8_t dataLength, uint8_t *data)
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

void updateFilter(MsgType msgType, uint8_t senderID, uint8_t receiverID, uint8_t header, FilterConfig config)
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