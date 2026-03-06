#include "blockchain.h"

#define BLOCKCHAIN_DIFFICULTY 5

bool verifyNonce(uint32_t nonce)
{
  bool result = true;
  uint8_t zeroes = 0;

  if(nonce == 0)
  {
    result = false;
  }

  while(result && nonce > 0 && zeroes != BLOCKCHAIN_DIFFICULTY)
  {
    if(nonce % 10 == 0)
    {
      zeroes++;
    }
    else
    {
      result = false;
    }

    nonce /= 10;
  }

  if(zeroes != BLOCKCHAIN_DIFFICULTY)
  {
    result = false;
  }

  return result;
}
