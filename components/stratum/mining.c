#include <string.h>

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
