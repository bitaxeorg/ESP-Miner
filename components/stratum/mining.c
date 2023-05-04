#include <string.h>
#include "mining.h"
#include "utils.h"

char * construct_coinbase_tx(const char * coinbase_1, const char * coinbase_2,
                             const char * extranonce, int extranonce_2_len)
{
    int coinbase_tx_len = strlen(coinbase_1) + strlen(coinbase_2)
        + strlen(extranonce) + extranonce_2_len * 2 + 1;

    char extranonce_2[extranonce_2_len * 2];
    memset(extranonce_2, '9', extranonce_2_len * 2);

    char * coinbase_tx = malloc(coinbase_tx_len * sizeof(char));
    strcpy(coinbase_tx, coinbase_1);
    strcat(coinbase_tx, extranonce);
    strcat(coinbase_tx, extranonce_2);
    strcat(coinbase_tx, coinbase_2);
    coinbase_tx[coinbase_tx_len - 1] = '\0';

    return coinbase_tx;
}

char * calculate_merkle_root_hash(const char * coinbase_tx, const uint8_t ** merkle_branches, const int num_merkle_branches)
{
    size_t coinbase_tx_bin_len = strlen(coinbase_tx) / 2;
    uint8_t * coinbase_tx_bin = malloc(coinbase_tx_bin_len);
    hex2bin(coinbase_tx, coinbase_tx_bin, coinbase_tx_bin_len);

    uint8_t both_merkles[64];
    uint8_t * new_root = double_sha256_bin(coinbase_tx_bin, coinbase_tx_bin_len);
    memcpy(both_merkles, new_root, 32);
    free(new_root);
    for (int i = 0; i < num_merkle_branches; i++)
    {
        memcpy(both_merkles + 32, merkle_branches + 32*i, 32);
        uint8_t * new_root = double_sha256_bin(both_merkles, 64);
        memcpy(both_merkles, new_root, 32);
        free(new_root);
    }

    char * merkle_root_hash = malloc(33);
    bin2hex(both_merkles, 32, merkle_root_hash, 33);
    return merkle_root_hash;
}