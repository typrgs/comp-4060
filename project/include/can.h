#pragma once

#include "common.h"

#define RX_FIFO_ELEMENT_DATA_BYTES 8

typedef void (*canCallback)(uint8_t);

// need to define message RAM area first, including rx FIFO 0 area, tx Buffers area, and extended filters area
void canInit( uint32_t *rxFifoStart, uint32_t *txBufStart, uint32_t *extendedFilterListStart, uint32_t extendedFilterListCount, uint8_t *buf, canCallback callback);
void canSend(uint32_t mask);
