#include "unity.h"
#include "mining.h"
#include "utils.h"

#include <limits.h>

TEST_CASE("Check coinbase tx construction", "[mining]")
{
    const char * coinbase_1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff20020862062f503253482f04b8864e5008";
    const char * coinbase_2 = "072f736c7573682f000000000100f2052a010000001976a914d23fcdf86f7e756a64a7a9688ef9903327048ed988ac00000000";
    const char * extranonce = "e9695791";
    const char * extranonce_2 = "99999999";
    char * coinbase_tx = construct_coinbase_tx(coinbase_1, coinbase_2, extranonce, extranonce_2);
    TEST_ASSERT_EQUAL_STRING(coinbase_tx, "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff20020862062f503253482f04b8864e5008e969579199999999072f736c7573682f000000000100f2052a010000001976a914d23fcdf86f7e756a64a7a9688ef9903327048ed988ac00000000");
    free(coinbase_tx);
}

// Values calculated from esp-miner/components/stratum/test/verifiers/merklecalc.py
TEST_CASE("Validate merkle root calculation", "[mining]")
{
    const char * coinbase_tx = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff20020862062f503253482f04b8864e5008e969579199999999072f736c7573682f000000000100f2052a010000001976a914d23fcdf86f7e756a64a7a9688ef9903327048ed988ac00000000";
    uint8_t merkles[12][32];
    int num_merkles = 12;

    hex2bin("ae23055e00f0f697cc3640124812d96d4fe8bdfa03484c1c638ce5a1c0e9aa81", merkles[0], 32);
    hex2bin("980fb87cb61021dd7afd314fcb0dabd096f3d56a7377f6f320684652e7410a21", merkles[1], 32);
    hex2bin("a52e9868343c55ce405be8971ff340f562ae9ab6353f07140d01666180e19b52", merkles[2], 32);
    hex2bin("7435bdfa004e603953b2ed39f118803934d9cf17b06d979ceb682f2251bafac2", merkles[3], 32);
    hex2bin("2a91f061a22d27cb8f44eea79938fb241ebeb359891aa907f05ffde7ed44e52e", merkles[4], 32);
    hex2bin("302401f80eb5e958155135e25200bb8ea181ad2d05e804a531c7314d86403cdc", merkles[5], 32);
    hex2bin("318ecb6161eb9b4cfd802bd730e2d36c167ddf102e70aa7b4158e2870dd47392", merkles[6], 32);
    hex2bin("1114332a9858e0cf84b2425bb1e59eaabf91dd102d114aa443d57fc1b3beb0c9", merkles[7], 32);
    hex2bin("f43f38095c810613ed795a44d9fab02ff25269706f454885db9be05cdf9c06e1", merkles[8], 32);
    hex2bin("3e2fc26b27fddc39668b59099cd9635761bb72ed92404204e12bdff08b16fb75", merkles[9], 32);
    hex2bin("463c19427286342120039a83218fa87ce45448e246895abac11fff0036076758", merkles[10], 32);
    hex2bin("03d287f655813e540ddb9c4e7aeb922478662b0f5d8e9d0cbd564b20146bab76", merkles[11], 32);

    char * root_hash = calculate_merkle_root_hash(coinbase_tx, merkles, num_merkles);
    TEST_ASSERT_EQUAL_STRING("adbcbc21e20388422198a55957aedfa0e61be0b8f2b87d7c08510bb9f099a893", root_hash);
    free(root_hash);
}

TEST_CASE("Validate another merkle root calculation", "[mining]")
{
    const char * coinbase_tx = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff2503777d07062f503253482f0405b8c75208f800880e000000000b2f436f696e48756e74722f0000000001603f352a010000001976a914c633315d376c20a973a758f7422d67f7bfed9c5888ac00000000";
    uint8_t merkles[5][32];
    int num_merkles = 5;

    hex2bin("f0dbca1ee1a9f6388d07d97c1ab0de0e41acdf2edac4b95780ba0a1ec14103b3", merkles[0], 32);
    hex2bin("8e43fd2988ac40c5d97702b7e5ccdf5b06d58f0e0d323f74dd5082232c1aedf7", merkles[1], 32);
    hex2bin("1177601320ac928b8c145d771dae78a3901a089fa4aca8def01cbff747355818", merkles[2], 32);
    hex2bin("9f64f3b0d9edddb14be6f71c3ac2e80455916e207ffc003316c6a515452aa7b4", merkles[3], 32);
    hex2bin("2d0b54af60fad4ae59ec02031f661d026f2bb95e2eeb1e6657a35036c017c595", merkles[4], 32);

    char * root_hash = calculate_merkle_root_hash(coinbase_tx, merkles, num_merkles);
    TEST_ASSERT_EQUAL_STRING("5cc58f5e84aafc740d521b92a7bf72f4e56c4cc3ad1c2159f1d094f97ac34eee", root_hash);
    free(root_hash);
}

// Values calculated from esp-miner/components/stratum/test/verifiers/bm1397.py
TEST_CASE("Validate bm job construction", "[mining]")
{
    mining_notify notify_message;
    notify_message.prev_block_hash = "bf44fd3513dc7b837d60e5c628b572b448d204a8000007490000000000000000";
    notify_message.version = 0x20000004;
    notify_message.target = 0x1705dd01;
    notify_message.ntime = 0x64658bd8;
    const char * merkle_root = "cd1be82132ef0d12053dcece1fa0247fcfdb61d4dbd3eb32ea9ef9b4c604a846";
    bm_job job = construct_bm_job(&notify_message, merkle_root);

    uint8_t expected_midstate_bin[32];
    hex2bin("91DFEA528A9F73683D0D495DD6DD7415E1CA21CB411759E3E05D7D5FF285314D", expected_midstate_bin, 32);
    // bytes are reversed for the midstate on the bm job command packet
    reverse_bytes(expected_midstate_bin, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_midstate_bin, job.midstate, 32);
}

TEST_CASE("Test extranonce 2 generation", "[mining extranonce2]")
{
    char * first = extranonce_2_generate(0, 4);
    TEST_ASSERT_EQUAL_STRING("00000000", first);
    free(first);

    char * second = extranonce_2_generate(1, 4);
    TEST_ASSERT_EQUAL_STRING("01000000", second);
    free(second);

    char * third = extranonce_2_generate(2, 4);
    TEST_ASSERT_EQUAL_STRING("02000000", third);
    free(third);

    char * fourth = extranonce_2_generate(UINT_MAX - 1, 4);
    TEST_ASSERT_EQUAL_STRING("feffffff", fourth);
    free(fourth);

    char * fifth = extranonce_2_generate(UINT_MAX / 2, 6);
    TEST_ASSERT_EQUAL_STRING("ffffff7f0000", fifth);
    free(fifth);
}

TEST_CASE("Test nonce diff checking", "[mining test_nonce]")
{
    mining_notify notify_message;
    notify_message.prev_block_hash = "d02b10fc0d4711eae1a805af50a8a83312a2215e00017f2b0000000000000000";
    notify_message.version = 0x20000004;
    notify_message.target = 0x1705ae3a;
    notify_message.ntime = 0x646ff1a9;
    const char * merkle_root = "C459036D054643519C5A2AC50B3474E0632C7CE4F93107843FDBF1EDD9CDB126646FF1A9";
    bm_job job = construct_bm_job(&notify_message, merkle_root);

    uint32_t nonce = 0x276E8947;
    double diff = test_nonce_value(&job, nonce);
    TEST_ASSERT_EQUAL_INT(18, (int) diff);
}