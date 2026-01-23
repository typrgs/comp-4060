#pragma once

#include "common.h"

// This unit requires the CAN trasceiver in either bus slot 2 or 3, with jumper cables from bus slot 1 TX and RX to the external UART TX and RX pads on the CAN trasceiver
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
  NUM_TYPES
} FilterType;

typedef void (*CANCallback)(uint8_t);

// need to define message RAM area first, including rx FIFO 0 area, tx Buffers area, and extended filters area
void CANInit(uint32_t *rxFifo0Start, uint32_t *rxFifo1Start, uint32_t *txBufStart, uint32_t *extendedFilterListStart, uint32_t rxFifo0Count, uint32_t rxFifo1Count, uint32_t txBufCount, uint32_t extendedFilterListCount, uint8_t *buf, CANCallback rxCallback);
void CANSend(uint32_t mask);
void CANUpdateTxBuf(uint8_t bufIndex, uint32_t id, uint32_t data);
void CANUpdateFilter(uint8_t filterIndex, uint32_t firstID, uint32_t secondID, FilterConfig config, FilterType type);
