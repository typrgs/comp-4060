#pragma once

#include "common.h"

// This unit requires the CAN trasceiver in either bus slot 2 or 3, with jumper cables from bus slot 1 TX and RX to the external UART TX and RX pads on the CAN trasceiver
// This unit configures and uses generic clock 5

#define EXTENDED_FILTER_WORDS 2 // 32-bit words
#define RX_FIFO_ELEMENT_WORDS 4 // 32-bit words
#define TX_BUF_ELEMENT_WORDS 4 // 32-bit words

#define RX_FIFO_ELEMENT_DATA_BYTES 8 // bytes

typedef void (*CANCallback)(uint8_t);

// need to define message RAM area first, including rx FIFO 0 area, tx Buffers area, and extended filters area
void CANInit(uint32_t *rxFifoStart, uint32_t *txBufStart, uint32_t *extendedFilterListStart, uint32_t rxFifoCount, uint32_t txBufCount, uint32_t extendedFilterListCount, uint8_t *buf, CANCallback rxCallback);
void CANSend(uint32_t mask);
