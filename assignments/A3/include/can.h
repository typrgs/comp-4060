#pragma once

#include "common.h"

/*
  This unit attempts to use all features of CAN at the physical layer without using the chip provided CAN unit.
  It does *not* implement the data link and application standards for CAN. It does try to mimic them as much as possible (mainly collision mgmt).
*/

typedef void(*CANCallback)(void);

// pass values for asynchronous packet reception
// *all* receiving is done asychronously so these values are *required*
void CANInit(uint8_t *data, uint8_t expectedSize, CANCallback done);

// returns true if data successfully received, false otherwise
bool CANSendBytes(uint8_t const * const bytes, uint16_t size);
