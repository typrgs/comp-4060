#include "hash.h"

#define MSB_MASK 0x80000000

uint32_t leftRotate(uint32_t value, uint8_t amount)
{
  for(int i=0; i<amount; i++)
  {
    // mask out MSB
    uint32_t msb = (value & MSB_MASK);

    // shift MSB out of value
    value <<= 1;

    // shift it over to LSB position
    msb >>= 31;

    // finish rotation
    value |= msb;
  }

  return value;
}
