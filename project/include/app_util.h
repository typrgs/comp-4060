#pragma once;

#include "common.h"
#include "net.h"
#include "can.h"

void setTxBufs(CANTxBuf *passedTxBufs);
void setFilters(CANExtFilter *passedFilters);
void updateTxBuf(MsgType type, uint8_t senderID, uint8_t receiverID, uint8_t header, uint8_t dataLength, uint8_t *data);
void updateFilter(MsgType msgType, uint8_t senderID, uint8_t receiverID, uint8_t header, FilterConfig config);