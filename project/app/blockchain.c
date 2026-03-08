#include "blockchain.h"
#include "icm.h"

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

static bool compareBlocks(Block a, Block b)
{
  if (a.height != b.height)
    return false;
  if (a.minerID != b.minerID)
    return false;
  if (a.nonce != b.nonce)
    return false;
  if (a.transaction.srcID != b.transaction.srcID)
    return false;

  for (uint8_t i = 0; i < TRANSACTION_MSG_SIZE; i++)
  {
    if (a.transaction.msg[i] != b.transaction.msg[i])
      return false;
  }

  for (uint16_t i = 0; i < BLOCK_HASH_SIZE; i++)
  {
    if (a.prevHash[i] != b.prevHash[i])
      return false;
  }

  return true;
}

static bool findBlock(Block *blockchain, uint16_t height, Block key)
{
  for (uint16_t i = 0; i < height; i++)
  {
    if (compareBlocks(blockchain[i], key))
    {
      return true;
    }
  }

  return false;
}

bool verifyBlock(Block *blockchain, uint16_t height, Block toVerify)
{
  if (findBlock(blockchain, height, toVerify))
  {
    dbg_write_str("Block already exists\n");
    return false;
  }
  else if (height == 0 && (toVerify.nonce != GENSIS_NONCE || toVerify.transaction.msgLen != GENESIS_MSG_LEN || toVerify.transaction.srcID != GENESIS_SRC_ID))
  {
    return false;
  }
  else if (height > 0)
  {
    // hash current top block for use in comparison
    uint8_t prevHash[BLOCK_HASH_SIZE] = {0};

    // msg length is sizeof(Block) * 2 because the length of a hex string needed to represent the size is twice the total amount of bytes
    // (each byte is represented by 8 bits = 2 hex digits)
    icmSHA256((uint8_t *)&(blockchain[height - 1]), sizeof(Block), prevHash);

    for (int i = 0; i < BLOCK_HASH_SIZE; i++)
    {
      if (toVerify.prevHash[i] != prevHash[i])
      {
        dbg_write_str("Prev hash does not match\n");
        return false;
      }
    }
  }

  return true;
}
