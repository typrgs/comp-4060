#ifndef _MY_SPI_H
#define _MY_SPI_H

#include "common.h"

// The SPI is running off of a 48MHz clock. Base all BAUD settings on this clock frequency.
// This unit configures and uses generic clock 3.

void spiInit();
void spiActivate(uint8_t baud, bool rx, bool idleHigh, bool leadingSample);
void spiDeactivate();

void spiWriteByte(uint8_t value);
// use a timeout of 0 to indicate blocking forever. value passed is milliseconds to timeout
uint8_t spiReadByte(uint16_t timeoutMS);

#endif