#pragma once

#include "common.h"
#include "icm.h"
#include "hmac.h"

#define BLOCKCHAIN_SIZE UINT8_MAX * 2
#define BLOCK_HASH_SIZE SHA256_DIGEST_SIZE
#define TRANSACTION_MSG_SIZE 20

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
  uint8_t signature[HMAC_SIZE];
} Transaction;

typedef struct BLOCK
{
  uint8_t minerID;
  Transaction transaction;
  uint8_t prevHash[BLOCK_HASH_SIZE];
  uint8_t height;
  uint32_t nonce;
} Block;

void signTransaction(Transaction *transaction, uint8_t *key, uint8_t keyLen);
bool verifyNonce(uint32_t nonce);
bool verifyBlock(Block *blockchain, uint16_t height, uint8_t *key, uint8_t keyLen, Block toVerify);
