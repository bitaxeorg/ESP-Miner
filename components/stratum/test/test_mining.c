#include "unity.h"
#include "mining.h"
#include "utils.h"

#include <limits.h>

TEST_CASE("Check coinbase tx construction", "[mining]")
{
    const char *coinbase_1 = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff20020862062f503253482f04b8864e5008";
    const char *coinbase_2 = "072f736c7573682f000000000100f2052a010000001976a914d23fcdf86f7e756a64a7a9688ef9903327048ed988ac00000000";
    const char *extranonce = "e9695791";
    const char *extranonce_2 = "99999999";
    char *coinbase_tx = construct_coinbase_tx(coinbase_1, coinbase_2, extranonce, extranonce_2);
    TEST_ASSERT_EQUAL_STRING(coinbase_tx, "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff20020862062f503253482f04b8864e5008e969579199999999072f736c7573682f000000000100f2052a010000001976a914d23fcdf86f7e756a64a7a9688ef9903327048ed988ac00000000");
    free(coinbase_tx);
}

// Values calculated from esp-miner/components/stratum/test/verifiers/merklecalc.py
TEST_CASE("Validate merkle root calculation", "[mining]")
{
    const char *coinbase_tx = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff20020862062f503253482f04b8864e5008e969579199999999072f736c7573682f000000000100f2052a010000001976a914d23fcdf86f7e756a64a7a9688ef9903327048ed988ac00000000";
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

    char root_hash[65];
    calculate_merkle_root_hash(coinbase_tx, merkles, num_merkles, root_hash);
    TEST_ASSERT_EQUAL_STRING("adbcbc21e20388422198a55957aedfa0e61be0b8f2b87d7c08510bb9f099a893", root_hash);
}

TEST_CASE("Validate another merkle root calculation", "[mining]")
{
    const char *coinbase_tx = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff2503777d07062f503253482f0405b8c75208f800880e000000000b2f436f696e48756e74722f0000000001603f352a010000001976a914c633315d376c20a973a758f7422d67f7bfed9c5888ac00000000";
    uint8_t merkles[5][32];
    int num_merkles = 5;

    hex2bin("f0dbca1ee1a9f6388d07d97c1ab0de0e41acdf2edac4b95780ba0a1ec14103b3", merkles[0], 32);
    hex2bin("8e43fd2988ac40c5d97702b7e5ccdf5b06d58f0e0d323f74dd5082232c1aedf7", merkles[1], 32);
    hex2bin("1177601320ac928b8c145d771dae78a3901a089fa4aca8def01cbff747355818", merkles[2], 32);
    hex2bin("9f64f3b0d9edddb14be6f71c3ac2e80455916e207ffc003316c6a515452aa7b4", merkles[3], 32);
    hex2bin("2d0b54af60fad4ae59ec02031f661d026f2bb95e2eeb1e6657a35036c017c595", merkles[4], 32);

    char root_hash[65];
    calculate_merkle_root_hash(coinbase_tx, merkles, num_merkles, root_hash);
    TEST_ASSERT_EQUAL_STRING("5cc58f5e84aafc740d521b92a7bf72f4e56c4cc3ad1c2159f1d094f97ac34eee", root_hash);
}

// Values calculated from esp-miner/components/stratum/test/verifiers/bm1397.py
TEST_CASE("Validate bm job construction", "[mining]")
{
    mining_notify notify_message;
    notify_message.prev_block_hash = "bf44fd3513dc7b837d60e5c628b572b448d204a8000007490000000000000000";
    notify_message.version = 0x20000004;
    notify_message.target = 0x1705dd01;
    notify_message.ntime = 0x64658bd8;
    const char *merkle_root = "cd1be82132ef0d12053dcece1fa0247fcfdb61d4dbd3eb32ea9ef9b4c604a846";
    bm_job job = construct_bm_job(&notify_message, merkle_root, 0, 1000);

    uint8_t expected_midstate_bin[32];
    hex2bin("91DFEA528A9F73683D0D495DD6DD7415E1CA21CB411759E3E05D7D5FF285314D", expected_midstate_bin, 32);
    // bytes are reversed for the midstate on the bm job command packet
    reverse_bytes(expected_midstate_bin, 32);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_midstate_bin, job.midstate, 32);
}

TEST_CASE("Validate version mask incrementing", "[mining]")
{
    uint32_t version = 0x20000004;
    uint32_t version_mask = 0x00ffff00;

    uint32_t rolled_version = increment_bitmask(version, version_mask);
    TEST_ASSERT_EQUAL_UINT32(0x20000104, rolled_version);
    rolled_version = increment_bitmask(rolled_version, version_mask);
    TEST_ASSERT_EQUAL_UINT32(0x20000204, rolled_version);
    rolled_version = increment_bitmask(rolled_version, version_mask);
    TEST_ASSERT_EQUAL_UINT32(0x20000304, rolled_version);
    rolled_version = increment_bitmask(rolled_version, version_mask);
    TEST_ASSERT_EQUAL_UINT32(0x20000404, rolled_version);
}

// Values calculated from esp-miner/components/stratum/test/verifiers/bm1397.py
// TEST_CASE("Validate bm job construction 2", "[mining]")
// {
//     const char * notify_json_str = "{\"id\":null,\"method\":\"mining.notify\","
//     "\"params\":[\"21554471e8\",\"8bc8707eb169ad3bda101ae60c8d48bd00aff68a00006c8b0000000000000000\",\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03d8130cfabe6d6db0ba74b36edc62c9268c945b53ebf1a7865b88bcdd40235a7a63d0f5ed5b6c400100000000000000\",\"e8714455212f736c7573682f00000000033de04728000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3ae8c3686251b5ced65b6a65ea3e0491ac2975cd87c02b0640d3ec3c20005167770000000000000000266a24aa21a9eddffbecb5ef0a46324a3dd902fa84509a38d2c91548768845db5d6c2de0e33f6100000000\","
//     "[\"8ef6b79382a1fc5152c7e69b2dd4e3795ed758d6fe7748ef4d96e3ad8ac180b8\",\"5b6e1cfecd94050b763c2c6a08d4caabd54daee665aa8e41b53b39ec76b62707\",\"f85b768f83fffbb3927f7f440cb57a5ed386f368aae88ad9b9d92e7bc1cdce15\",\"e51b9391c39019d8a2a27becc048cc770f5d33b49a29779fdc7bed04767ca962\",\"9f5d08316ead260455ec532a58935411a3eecf3c9948a52325de495d7dd7b776\",\"57e10cad23a646ad3a87fcd34eae454567dbd44946e746ee6310a86b98afa4ac\",\"f3b65cc08b25901b657efb22f0a9a23e1a61ce1e268f801d8cfe782b4a0c5e5d\",\"648e00fe2a57dca155c7d4260bc52273b28adb42e1bceb45d5ee03f4a4c5d174\",\"43ad393f7efe4b7a29775dbbc10b3b2737e9457764a7b39bc8ac6b470b968ac8\",\"4964b9b2bf601dfb2bd62067acafe556650412b1e6fe32df48c39310f7dc255d\",\"44d354ac57fcb68b408df7f5396122195384914dd2db13d5766c334fc48c2069\",\"568514a2db82a055772218f52db2f5fa157c37a9ba16c1a239819e57f0d16218\"],"
//     "\"20000004\",\"1705ae3a\",\"6470e2a1\",true]}";
//     mining_notify * params = parse_mining_notify_message(notify_json_str, 512);
//     char * coinbase_tx = construct_coinbase_tx(params->coinbase_1, params->coinbase_2, "336508070fca95", "0000000000000000");
//     char merkle_root[65]
//     calculate_merkle_root_hash(coinbase_tx, (uint8_t(*)[32])params->merkle_branches, params->n_merkle_branches, merkle_root);
//     bm_job job = construct_bm_job(params, merkle_root, 1000);

//     uint8_t expected_midstate_bin[32];
//     hex2bin("5FD281AF6A1750EAEE502C04067738BD46C82FC22112FFE797CE7F035D276126", expected_midstate_bin, 32);
//     // bytes are reversed for the midstate on the bm job command packet
//     reverse_bytes(expected_midstate_bin, 32);
//     TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_midstate_bin, job.midstate, 32);
//     TEST_ASSERT_EQUAL_UINT32(0x1705ae3a, job.target);
//     TEST_ASSERT_EQUAL_UINT32(0x6470e2a1, job.ntime);
//     TEST_ASSERT_EQUAL_UINT8(0x8a, job.merkle_root[28]);
//     TEST_ASSERT_EQUAL_UINT8(0xdd, job.merkle_root[29]);
//     TEST_ASSERT_EQUAL_UINT8(0xa8, job.merkle_root[30]);
//     TEST_ASSERT_EQUAL_UINT8(0x6a, job.merkle_root[31]);
// }

TEST_CASE("Test extranonce 2 generation", "[mining extranonce2]")
{
    char first[9];
    extranonce_2_generate(0, 4, first);
    TEST_ASSERT_EQUAL_STRING("00000000", first);

    char second[9];
    extranonce_2_generate(1, 4, second);
    TEST_ASSERT_EQUAL_STRING("01000000", second);

    char third[9];
    extranonce_2_generate(2, 4, third);
    TEST_ASSERT_EQUAL_STRING("02000000", third);

    char fourth[9];
    extranonce_2_generate(UINT_MAX - 1, 4, fourth);
    TEST_ASSERT_EQUAL_STRING("feffffff", fourth);

    char fifth[13];
    extranonce_2_generate(UINT_MAX / 2, 6, fifth);
    TEST_ASSERT_EQUAL_STRING("ffffff7f0000", fifth);
}

TEST_CASE("Test nonce diff checking", "[mining test_nonce][not-on-qemu]")
{
    mining_notify notify_message;
    notify_message.prev_block_hash = "d02b10fc0d4711eae1a805af50a8a83312a2215e00017f2b0000000000000000";
    notify_message.version = 0x20000004;
    notify_message.target = 0x1705ae3a;
    notify_message.ntime = 0x646ff1a9;
    const char *merkle_root = "6d0359c451434605c52a5a9ce074340be47c2c63840731f9edf1db3f26b1cdd9a9f16f64";
    bm_job job = construct_bm_job(&notify_message, merkle_root, 0, 1000);

    uint32_t nonce = 0x276E8947;
    double diff = test_nonce_value(&job, nonce, 0);
    TEST_ASSERT_EQUAL_INT(18, (int)diff);
}

TEST_CASE("Test nonce diff checking 2", "[mining test_nonce][not-on-qemu]")
{
    mining_notify notify_message;
    notify_message.prev_block_hash = "0c859545a3498373a57452fac22eb7113df2a465000543520000000000000000";
    notify_message.version = 0x20000004;
    notify_message.target = 0x1705ae3a;
    notify_message.ntime = 0x647025b5;

    const char *coinbase_tx = "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b0389130cfabe6d6d5cbab26a2599e92916edec5657a94a0708ddb970f5c45b5d12905085617eff8e010000000000000031650707758de07b010000000000001cfd7038212f736c7573682f000000000379ad0c2a000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3ae725d3994b811572c1f345deb98b56b465ef8e153ecbbd27fa37bf1b005161380000000000000000266a24aa21a9ed63b06a7946b190a3fda1d76165b25c9b883bcc6621b040773050ee2a1bb18f1800000000";
    uint8_t merkles[13][32];
    int num_merkles = 13;

    hex2bin("2b77d9e413e8121cd7a17ff46029591051d0922bd90b2b2a38811af1cb57a2b2", merkles[0], 32);
    hex2bin("5c8874cef00f3a233939516950e160949ef327891c9090467cead995441d22c5", merkles[1], 32);
    hex2bin("2d91ff8e19ac5fa69a40081f26c5852d366d608b04d2efe0d5b65d111d0d8074", merkles[2], 32);
    hex2bin("0ae96f609ad2264112a0b2dfb65624bedbcea3b036a59c0173394bba3a74e887", merkles[3], 32);
    hex2bin("e62172e63973d69574a82828aeb5711fc5ff97946db10fc7ec32830b24df7bde", merkles[4], 32);
    hex2bin("adb49456453aab49549a9eb46bb26787fb538e0a5f656992275194c04651ec97", merkles[5], 32);
    hex2bin("a7bc56d04d2672a8683892d6c8d376c73d250a4871fdf6f57019bcc737d6d2c2", merkles[6], 32);
    hex2bin("d94eceb8182b4f418cd071e93ec2a8993a0898d4c93bc33d9302f60dbbd0ed10", merkles[7], 32);
    hex2bin("5ad7788b8c66f8f50d332b88a80077ce10e54281ca472b4ed9bbbbcb6cf99083", merkles[8], 32);
    hex2bin("9f9d784b33df1b3ed3edb4211afc0dc1909af9758c6f8267e469f5148ed04809", merkles[9], 32);
    hex2bin("48fd17affa76b23e6fb2257df30374da839d6cb264656a82e34b350722b05123", merkles[10], 32);
    hex2bin("c4f5ab01913fc186d550c1a28f3f3e9ffaca2016b961a6a751f8cca0089df924", merkles[11], 32);
    hex2bin("cff737e1d00176dd6bbfa73071adbb370f227cfb5fba186562e4060fcec877e1", merkles[12], 32);

    char merkle_root[65];
    calculate_merkle_root_hash(coinbase_tx, merkles, num_merkles, merkle_root);
    TEST_ASSERT_EQUAL_STRING("5bdc1968499c3393873edf8e07a1c3a50a97fc3a9d1a376bbf77087dd63778eb", merkle_root);

    bm_job job = construct_bm_job(&notify_message, merkle_root, 0, 1000);

    uint32_t nonce = 0x0a029ed1;
    double diff = test_nonce_value(&job, nonce, 0);
    TEST_ASSERT_EQUAL_INT(683, (int)diff);
}
