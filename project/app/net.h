#pragma once

#define BROADCAST_ID 0

#define ID_MSG_TYPE_Pos 0
#define ID_SENDER_Pos 1
#define ID_RECEIVER_Pos 2
#define ID_HEADER_Pos 3

typedef enum MSG_TYPE
{
  MSG_DISCOVER,
  MSG_CHAIN,
  MSG_NEW,
  MSG_BLOCK,
  NUM_MSG_TYPES
} MsgType;

typedef enum HEADER_TYPE
{
  HDR_NONE,
  HDR_CHAIN,
  HDR_NEW,
  NUM_HEADER_TYPES
} HeaderType;
