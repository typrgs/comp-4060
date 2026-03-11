#pragma once

#include "common.h"

#define HMAC_SIZE 32 // bytes

void HMACSign(uint8_t *msg, uint64_t msgLen, uint8_t *key, uint8_t keyLen, uint8_t *signature);
bool HMACVerify(uint8_t *msg, uint64_t msgLen, uint8_t *key, uint8_t keyLen, uint8_t *signature);