#include "hmac.h"
#include "icm.h"

#define OPAD_VAL 0x5C
#define IPAD_VAL 0x36

void HMACSign(uint8_t *msg, uint64_t msgLen, uint8_t *key, uint8_t keyLen, uint8_t *signature)
{
  uint8_t outerKey[SHA256_BLOCK_SIZE] = {0};
  uint8_t innerKey[SHA256_BLOCK_SIZE] = {0};
  uint8_t innerMsg[SHA256_BLOCK_SIZE + msgLen];
  uint8_t outerMsg[SHA256_BLOCK_SIZE + SHA256_DIGEST_SIZE];
  uint8_t innerHash[HMAC_SIZE] = {0};

  if (keyLen > SHA256_BLOCK_SIZE)
  {
    icmSHA256(key, keyLen, &outerKey[HMAC_SIZE]);
    icmSHA256(key, keyLen, &innerKey[HMAC_SIZE]);
  }
  else
  {
    for (uint8_t i = 0; i < keyLen; i++)
    {
      outerKey[(HMAC_SIZE - keyLen) + i] = key[i];
      innerKey[(HMAC_SIZE - keyLen) + i] = key[i];
    }
  }

  for (uint8_t i = 0; i < SHA256_BLOCK_SIZE; i++)
  {
    outerKey[i] ^= OPAD_VAL;
    innerKey[i] ^= IPAD_VAL;
  }

  uint8_t innerMsgPos = 0;
  uint8_t outerMsgPos = 0;
  for (uint8_t i = 0; i < SHA256_BLOCK_SIZE; i++)
  {
    innerMsg[innerMsgPos++] = innerKey[i];
    outerMsg[outerMsgPos++] = outerKey[i];
  }

  for (uint8_t i = 0; i < msgLen; i++)
  {
    innerMsg[innerMsgPos++] = msg[i];
  }

  icmSHA256(innerMsg, SHA256_BLOCK_SIZE + msgLen, innerHash);

  for (uint8_t i = 0; i < SHA256_DIGEST_SIZE; i++)
  {
    outerMsg[outerMsgPos++] = innerHash[i];
  }

  icmSHA256(outerMsg, SHA256_BLOCK_SIZE + SHA256_DIGEST_SIZE, signature);
}

bool HMACVerify(uint8_t *msg, uint64_t msgLen, uint8_t *key, uint8_t keyLen, uint8_t *signature)
{
  bool verifyResult = true;
  uint8_t signResult[HMAC_SIZE];
  sign(msg, msgLen, key, keyLen, signResult);

  for (int i = 0; i < HMAC_SIZE && verifyResult; i++)
  {
    if (signature[i] != signResult[i])
    {
      verifyResult = false;
    }
  }

  return verifyResult;
}
