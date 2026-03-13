#include "blockchain.h"

static uint8_t blockchainDiff = 0;
static blockchainMACSign sign = NULL;
static blockchainMACVerify verify = NULL;
static blockchainHash hash = NULL;

void blockchainInit(uint8_t difficulty, blockchainMACSign MACSign, blockchainMACVerify MACVerify, blockchainHash hashF)
{
  blockchainDiff = difficulty;
  sign = MACSign;
  verify = MACVerify;
  hash = hashF;
}

bool verifyNonce(uint32_t nonce)
{
  bool result = true;

  if (nonce == 0)
  {
    result = false;
  }

  for (int i = 0; i < blockchainDiff && result; i++)
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
  bool result = true;

  if (a.height != b.height)
    result = false;
  else if (a.minerID != b.minerID)
    result = false;
  else if (a.nonce != b.nonce)
    result = false;
  else if (a.transaction.srcID != b.transaction.srcID)
    result = false;
  else
  {
    for (uint8_t i = 0; i < TRANSACTION_MSG_SIZE && result; i++)
    {
      if (a.transaction.msg[i] != b.transaction.msg[i])
        result = false;
    }

    for (uint16_t i = 0; i < BLOCK_MAX_HASH_SIZE && result; i++)
    {
      if (a.prevHash[i] != b.prevHash[i])
        result = false;
    }
  }

  return result;
}

static bool findBlock(Block *blockchain, uint16_t height, Block key)
{
  bool result = false;

  for (uint16_t i = 0; i < height && !result; i++)
  {
    if (compareBlocks(blockchain[i], key))
    {
      result = true;
    }
  }

  return result;
}

static void fillMACMsg(Transaction transaction, uint8_t *buf, uint8_t bufLen)
{
  for (uint8_t i = 0; i < bufLen - 1; i++)
  {
    buf[i] = transaction.msg[i];
  }

  buf[bufLen - 1] = transaction.srcID;
}

static bool fillAndVerifyMACMsg(Transaction transaction, uint8_t *buf, uint8_t bufLen, uint8_t *key, uint8_t keyLen, uint8_t *signature)
{
  fillMACMsg(transaction, buf, bufLen);
  return verify(buf, bufLen, key, keyLen, signature);
}

void signTransaction(Transaction *transaction, uint8_t *key, uint8_t keyLen)
{
  uint8_t toSignLen = transaction->msgLen + 1;
  uint8_t toSign[toSignLen];

  fillMACMsg(*transaction, toSign, toSignLen);
  sign(toSign, toSignLen, key, keyLen, transaction->signature);
}

BlockError verifyBlock(Block *blockchain, uint16_t height, uint8_t *key, uint8_t keyLen, Block toVerify)
{
  BlockError result = BLOCK_ERR_VALID;
  uint8_t msgToVerifyLen = toVerify.transaction.msgLen + 1;
  uint8_t msgToVerify[msgToVerifyLen];

  if (height == 0 && (toVerify.nonce != GENSIS_NONCE || toVerify.transaction.msgLen != GENESIS_MSG_LEN || toVerify.transaction.srcID != GENESIS_SRC_ID))
  {
    dbg_write_str("Bad genesis block\n");
    result = BLOCK_ERR_GENESIS;
  }
  else if (height > 0)
  {
    if (findBlock(blockchain, height, toVerify))
    {
      dbg_write_str("Block already exists\n");
      result = BLOCK_ERR_EXISTS;
    }
    else if (toVerify.height > height)
    {
      dbg_write_str("Received block is taller\n");
      result = BLOCK_ERR_TALLER;
    }
    else if (toVerify.height < height)
    {
      dbg_write_str("Received block is shorter\n");
      result = BLOCK_ERR_SHORTER;
    }
    else if (!verifyNonce(toVerify.nonce))
    {
      dbg_write_str("Received block has incorrect difficulty\n");
      result = BLOCK_ERR_DIFF;
    }
    else if (!fillAndVerifyMACMsg(toVerify.transaction, msgToVerify, msgToVerifyLen, key, keyLen, toVerify.transaction.signature))
    {
      dbg_write_str("Received block has incorrect signature\n");
      result = BLOCK_ERR_SIG;
    }
    else
    {
      // hash current top block for use in comparison
      uint8_t prevHash[BLOCK_MAX_HASH_SIZE] = {0};
  
      // msg length is sizeof(Block) * 2 because the length of a hex string needed to represent the size is twice the total amount of bytes
      // (each byte is represented by 8 bits = 2 hex digits)
      hash((uint8_t *)&(blockchain[height - 1]), sizeof(Block), prevHash);
  
      for (int i = 0; i < BLOCK_MAX_HASH_SIZE && result != BLOCK_ERR_HASH; i++)
      {
        if (toVerify.prevHash[i] != prevHash[i])
        {
          dbg_write_str("Prev hash does not match\n");
          result = BLOCK_ERR_HASH;
        }
      }
    }
  }

  return result;
}
