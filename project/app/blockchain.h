#pragma once

#include "common.h"

#define BLOCKCHAIN_SIZE UINT8_MAX * 2
#define BLOCK_MAX_HASH_SIZE 32      // bytes
#define TRANSACTION_MSG_SIZE 20     // bytes
#define TRANSACTION_MAX_SIG_SIZE 32 // bytes

#define GENESIS_MSG "HELLOWORLD"
#define GENESIS_MSG_LEN 10
#define GENESIS_SRC_ID 0
#define GENSIS_NONCE UINT32_MAX

#define PEER_PROPAGATION_COUNT 2

typedef struct TRANSACTION
{
  uint8_t srcID;
  char msg[TRANSACTION_MSG_SIZE];
  uint8_t msgLen;
  uint8_t signature[TRANSACTION_MAX_SIG_SIZE];
} Transaction;

typedef struct BLOCK
{
  uint8_t minerID;
  Transaction transaction;
  uint8_t prevHash[BLOCK_MAX_HASH_SIZE];
  uint8_t height;
  uint32_t nonce;
} Block;

typedef enum BLOCK_ERROR
{
  BLOCK_ERR_INCOMPLETE,
  BLOCK_ERR_VALID,
  BLOCK_ERR_GENESIS,
  BLOCK_ERR_EXISTS,
  BLOCK_ERR_SHORTER,
  BLOCK_ERR_TALLER,
  BLOCK_ERR_DIFF,
  BLOCK_ERR_SIG,
  BLOCK_ERR_HASH,
  NUM_BLOCK_ERRORS
} BlockError;

typedef void (*blockchainMACSign)(uint8_t *msg, uint64_t msgLen, uint8_t *key, uint8_t keyLen, uint8_t *signature);
typedef bool (*blockchainMACVerify)(uint8_t *msg, uint64_t msgLen, uint8_t *key, uint8_t keyLen, uint8_t *signature);
typedef void (*blockchainHash)(uint8_t *msg, uint64_t msgLen, uint8_t *result);

void blockchainInit(uint8_t difficulty, blockchainMACSign sign, blockchainMACVerify verify, blockchainHash hash);
void signTransaction(Transaction *transaction, uint8_t *key, uint8_t keyLen);
bool verifyNonce(uint32_t nonce);
BlockError verifyBlock(Block *blockchain, uint16_t height, uint8_t *key, uint8_t keyLen, Block toVerify);
