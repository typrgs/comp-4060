#pragma once

#include "common.h"

// This unit requires the CAN transceiver in bus slot 2, with jumper cables from bus slot 1 TX and RX to the external UART TX and RX pads on the CAN transceiver
// This unit configures and uses generic clock 5

#define EXTENDED_FILTER_WORDS 2 // 32-bit words
#define RX_FIFO_ELEMENT_WORDS 4 // 32-bit words
#define TX_BUF_ELEMENT_WORDS 4 // 32-bit words

#define RX_FIFO_ELEMENT_DATA_BYTES 8 // bytes

typedef enum FILTER_CONFIG
{
  DISABLE,
  STF0M,
  STF1M,
  REJECT,
  NUM_CONFIGS
} FilterConfig;

typedef enum FILTER_TYPE
{
  RANGE,
  DUAL,
  CLASSIC,
  NUM_TYPES
} FilterType;

typedef struct CAN_TX_BUF
{
  uint8_t bufIndex;
  uint8_t id[4];
  uint8_t dataLength;
  uint8_t data[8];
} CANTxBuf;

typedef struct CAN_EXT_FILTER
{
  uint8_t filterIndex;
  uint32_t firstID;
  uint32_t secondID;
  FilterConfig config;
  FilterType type;
} CANExtFilter;

typedef void (*CANCallback)(uint8_t, uint32_t);

void CANInit(uint32_t *rxFifo0Start, uint32_t *rxFifo1Start, uint32_t *txBufStart, uint32_t *extendedFilterListStart, uint32_t rxFifo0Count, uint32_t rxFifo1Count, uint32_t txBufCount, uint32_t extendedFilterListCount, uint8_t *buf, CANCallback rxCallback);
void CANSend(uint8_t index);
void CANUpdateTxBuf(CANTxBuf buf);
void CANUpdateFilter(CANExtFilter filter);
