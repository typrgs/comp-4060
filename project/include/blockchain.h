#pragma once

#include "common.h"

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
  uint8_t hash[64];
  uint8_t height;
  uint32_t nonce;
} Block;
