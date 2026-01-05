#ifndef _MY_TWI_H
#define _MY_TWI_H

#include "common.h"

// The TWI is running off of a 6MHz clock. Base all BAUD settings on this clock frequency.
// This unit configures and uses generic clock 4.

void twiInit();
void twiActivate(uint8_t baud);
void twiDeactivate();

// returns whether or not the device with the given address is on the bus
bool twiProbe(uint8_t addr);

void twiWriteBytes(uint8_t addr, uint8_t const * const bytes, uint16_t size, bool repeatStart);
void twiReadBytes(uint8_t addr, uint8_t * const bytes, uint8_t size);

#endif