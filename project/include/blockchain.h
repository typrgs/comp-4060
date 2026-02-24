#pragma once

#include "common.h"

#define BLOCKCHAIN_SIZE UINT8_MAX * 2
#define BLOCK_HASH_SIZE 32 // bytes
#define TRANSACTION_MSG_SIZE 100

typedef struct TRANSACTION
{
  uint8_t srcID;
  char msg[TRANSACTION_MSG_SIZE];
  uint8_t msgLen;
} Transaction;

typedef struct BLOCK
{
  uint8_t minerID;
  Transaction transaction;
  uint8_t prevHash[BLOCK_HASH_SIZE];
  uint8_t height;
  uint32_t nonce;
} Block;
