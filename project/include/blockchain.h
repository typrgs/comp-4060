#pragma once

#include "common.h"

#define BLOCK_HASH_SIZE 32 // bytes

typedef struct TRANSACTION
{
  uint8_t srcID;
  uint8_t destID;
  uint32_t amt;
} Transaction;

typedef struct BLOCK
{
  uint8_t minerID;
  Transaction transaction;
  uint8_t prevHash[32];
  uint8_t height;
  uint32_t nonce;
} Block;
