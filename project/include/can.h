#pragma once

#include "common.h"

// need to define message RAM area first, including rx FIFO 0 area, tx Buffers area, and extended filters area
void canInit(uint32_t *messageRam, uint32_t *rxFifoStart, uint32_t *txBufStart, uint32_t *extendedFilterListStart, uint32_t extendedFilterListCount);
void canSend(uint8_t *bytes);
