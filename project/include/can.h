#pragma once

#include "common.h"

// This unit requires the CAN transceiver in either bus slot 2, with jumper cables from bus slot 1 TX and RX to the external UART TX and RX pads on the CAN transceiver
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

void CANInit(uint32_t *rxFifo0Start, uint32_t *rxFifo1Start, uint32_t *txBufStart, uint32_t *extendedFilterListStart, uint8_t *buf, CANCallback rxCallback);
void CANSend(uint32_t mask);
void CANUpdateTxBuf(uint8_t bufIndex, uint32_t id, uint8_t dataLength, uint32_t firstData, uint32_t secondData);
void CANUpdateFilter(uint8_t filterIndex, uint32_t firstID, uint32_t secondID, FilterConfig config, FilterType type);
void CANSetFifo0Size(uint8_t size);
void CANSetFifo1Size(uint8_t size);
void CANSetTxBufSize(uint8_t size);
void CANSetFilterListSize(uint8_t size);
