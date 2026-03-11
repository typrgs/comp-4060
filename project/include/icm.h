#pragma once

#include "common.h"

#define SHA256_BLOCK_SIZE 64 // bytes
#define SHA256_DIGEST_SIZE 32 // bytes

void icmInit();
void icmSHA256(uint8_t *msg, uint64_t msgLen, uint8_t *result);
