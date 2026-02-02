#pragma once

#define BROADCAST_ID 0

typedef enum MSG_TYPE
{
  PULSE,
  CONSENSUS,
  SHARE,
  BLOCK,
  END,
  ACK,
  NUM_MSG_TYPES
} MsgType;
