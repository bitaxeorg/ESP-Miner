#ifndef MINING_H
#define MINING_H

#include "stratum_api.h"

char * construct_coinbase_tx(const char * coinbase_1, const char * coinbase_2,
                             const char * extranonce, int extranonce_2_len);

char * calculate_merkle_hash(const char * coinbase_tx, const uint8_t ** merkle_branches);

#endif // MINING_H