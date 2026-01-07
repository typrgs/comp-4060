#ifndef _MY_485_UART_H
#define _MY_485_UART_H

#include "common.h"

/*
  With RS-485 you cannot tx and rx at the same time. This means that there is no way for a transmitter to detect
  that a collision has occurred. We rely on the receiver to detect errors and respond (or not) accordingly. It
  is then up to the transmitter to wait for a response and act accordingly based on what it sees on the bus.
  This is also why a timeout is required; so the transmitter can react to the receiver staying quiet due to tx errors.

  This unit configures and uses generic clock 2
*/

typedef void(*uart485Callback)(void);

// pass values to allow asynchronous packet reception
// passing NULL for data and done, 0 for size, to indicate that synchronous receives will be used (with explicit calls)
void uart485Init(uint8_t *data, uint8_t expectedSize, uart485Callback done);

void uart485SendBytes(uint8_t const * const bytes, uint16_t size);
// use a timeout of 0 to indicate blocking forever. value passed is milliseconds to timeout
// returns true if data successfully received, false otherwise
bool uart485ReceiveBytes(uint8_t * const bytes, uint16_t size, uint16_t timeoutMS);

#endif