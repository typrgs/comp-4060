#include "blockchain.h"
#include "icm.h"

#define BLOCKCHAIN_DIFFICULTY 5

bool verifyNonce(uint32_t nonce)
{
  bool result = true;

  if (nonce == 0)
  {
    result = false;
  }

  for (int i = 0; i < BLOCKCHAIN_DIFFICULTY && result; i++)
  {
    if (nonce % 10 != 0)
    {
      result = false;
    }
    else
    {
      nonce /= 10;
    }
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
  bool result = true;

  if (findBlock(blockchain, height, toVerify))
  {
    dbg_write_str("Block already exists\n");
    result = false;
  }
  else if (height == 0 && (toVerify.nonce != GENSIS_NONCE || toVerify.transaction.msgLen != GENESIS_MSG_LEN || toVerify.transaction.srcID != GENESIS_SRC_ID))
  {
    result = false;
  }
  else if (height > 0)
  {
    if(toVerify.height != height)
    {
      result = false;
    }
    else if (!verifyNonce(toVerify.nonce))
    {
      result = false;
    }
    else
    {
      // hash current top block for use in comparison
      uint8_t prevHash[BLOCK_HASH_SIZE] = {0};

      // msg length is sizeof(Block) * 2 because the length of a hex string needed to represent the size is twice the total amount of bytes
      // (each byte is represented by 8 bits = 2 hex digits)
      icmSHA256((uint8_t *)&(blockchain[height - 1]), sizeof(Block), prevHash);

      for (int i = 0; i < BLOCK_HASH_SIZE && result; i++)
      {
        if (toVerify.prevHash[i] != prevHash[i])
        {
          dbg_write_str("Prev hash does not match\n");
          result = false;
        }
      }
    }
  }

  return result;
}
