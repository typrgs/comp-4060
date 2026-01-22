#pragma once

#include "common.h"

#define EXTENDED_FILTER_WORDS 2 // 32-bit words
#define RX_FIFO_ELEMENT_WORDS 4 // 32-bit words
#define TX_BUF_ELEMENT_WORDS 4 // 32-bit words

#define RX_FIFO_ELEMENT_DATA_BYTES 8 // bytes

typedef void (*canCallback)(uint8_t);

// need to define message RAM area first, including rx FIFO 0 area, tx Buffers area, and extended filters area
void canInit(uint32_t *rxFifoStart, uint32_t *txBufStart, uint32_t *extendedFilterListStart, uint32_t rxFifoCount, uint32_t txBufCount, uint32_t extendedFilterListCount, uint8_t *buf, canCallback rxCallback);
void canSend(uint32_t mask);
