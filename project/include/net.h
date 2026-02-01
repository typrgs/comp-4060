#pragma once

typedef enum MSG_TYPE
{
  PULSE,
  CONSENSUS,
  BLOCK,
  NEW_BLOCK,
  END,
  ACK,
  NUM_MSG_TYPES
} MsgType;
