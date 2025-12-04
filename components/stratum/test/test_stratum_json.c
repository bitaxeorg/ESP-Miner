#include "unity.h"
#include "stratum_api.h"

TEST_CASE("Parse stratum method", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};

    const char *json_string_standard = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                                       "[\"1b4c3d9041\","
                                       "\"ef4b9a48c7986466de4adc002f7337a6e121bc43000376ea0000000000000000\","
                                       "\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03a5020cfabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000\","
                                       "\"41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000\","
                                       "[\"ae23055e00f0f697cc3640124812d96d4fe8bdfa03484c1c638ce5a1c0e9aa81\",\"980fb87cb61021dd7afd314fcb0dabd096f3d56a7377f6f320684652e7410a21\",\"a52e9868343c55ce405be8971ff340f562ae9ab6353f07140d01666180e19b52\",\"7435bdfa004e603953b2ed39f118803934d9cf17b06d979ceb682f2251bafac2\",\"2a91f061a22d27cb8f44eea79938fb241ebeb359891aa907f05ffde7ed44e52e\",\"302401f80eb5e958155135e25200bb8ea181ad2d05e804a531c7314d86403cdc\",\"318ecb6161eb9b4cfd802bd730e2d36c167ddf102e70aa7b4158e2870dd47392\",\"1114332a9858e0cf84b2425bb1e59eaabf91dd102d114aa443d57fc1b3beb0c9\",\"f43f38095c810613ed795a44d9fab02ff25269706f454885db9be05cdf9c06e1\",\"3e2fc26b27fddc39668b59099cd9635761bb72ed92404204e12bdff08b16fb75\",\"463c19427286342120039a83218fa87ce45448e246895abac11fff0036076758\",\"03d287f655813e540ddb9c4e7aeb922478662b0f5d8e9d0cbd564b20146bab76\"],"
                                       "\"20000004\",\"1705c739\",\"64495522\",false]}";

    STRATUM_V1_parse(&stratum_api_v1_message, json_string_standard);
    TEST_ASSERT_EQUAL(MINING_NOTIFY, stratum_api_v1_message.method);
    TEST_ASSERT_FALSE(stratum_api_v1_message.mining_notification->clean_jobs);
}

TEST_CASE("Parse stratum mining.notify abandon work", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};

    const char *json_string_abandon_work_false = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                                                 "[\"1b4c3d9041\","
                                                 "\"ef4b9a48c7986466de4adc002f7337a6e121bc43000376ea0000000000000000\","
                                                 "\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03a5020cfabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000\","
                                                 "\"41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000\","
                                                 "[\"ae23055e00f0f697cc3640124812d96d4fe8bdfa03484c1c638ce5a1c0e9aa81\",\"980fb87cb61021dd7afd314fcb0dabd096f3d56a7377f6f320684652e7410a21\",\"a52e9868343c55ce405be8971ff340f562ae9ab6353f07140d01666180e19b52\",\"7435bdfa004e603953b2ed39f118803934d9cf17b06d979ceb682f2251bafac2\",\"2a91f061a22d27cb8f44eea79938fb241ebeb359891aa907f05ffde7ed44e52e\",\"302401f80eb5e958155135e25200bb8ea181ad2d05e804a531c7314d86403cdc\",\"318ecb6161eb9b4cfd802bd730e2d36c167ddf102e70aa7b4158e2870dd47392\",\"1114332a9858e0cf84b2425bb1e59eaabf91dd102d114aa443d57fc1b3beb0c9\",\"f43f38095c810613ed795a44d9fab02ff25269706f454885db9be05cdf9c06e1\",\"3e2fc26b27fddc39668b59099cd9635761bb72ed92404204e12bdff08b16fb75\",\"463c19427286342120039a83218fa87ce45448e246895abac11fff0036076758\",\"03d287f655813e540ddb9c4e7aeb922478662b0f5d8e9d0cbd564b20146bab76\"],"
                                                 "\"20000004\",\"1705c739\",\"64495522\",false]}";

    STRATUM_V1_parse(&stratum_api_v1_message, json_string_abandon_work_false);
    TEST_ASSERT_EQUAL(MINING_NOTIFY, stratum_api_v1_message.method);
    TEST_ASSERT_FALSE(stratum_api_v1_message.mining_notification->clean_jobs);

    const char *json_string_abandon_work = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                                           "[\"1b4c3d9041\","
                                           "\"ef4b9a48c7986466de4adc002f7337a6e121bc43000376ea0000000000000000\","
                                           "\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03a5020cfabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000\","
                                           "\"41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000\","
                                           "[\"ae23055e00f0f697cc3640124812d96d4fe8bdfa03484c1c638ce5a1c0e9aa81\",\"980fb87cb61021dd7afd314fcb0dabd096f3d56a7377f6f320684652e7410a21\",\"a52e9868343c55ce405be8971ff340f562ae9ab6353f07140d01666180e19b52\",\"7435bdfa004e603953b2ed39f118803934d9cf17b06d979ceb682f2251bafac2\",\"2a91f061a22d27cb8f44eea79938fb241ebeb359891aa907f05ffde7ed44e52e\",\"302401f80eb5e958155135e25200bb8ea181ad2d05e804a531c7314d86403cdc\",\"318ecb6161eb9b4cfd802bd730e2d36c167ddf102e70aa7b4158e2870dd47392\",\"1114332a9858e0cf84b2425bb1e59eaabf91dd102d114aa443d57fc1b3beb0c9\",\"f43f38095c810613ed795a44d9fab02ff25269706f454885db9be05cdf9c06e1\",\"3e2fc26b27fddc39668b59099cd9635761bb72ed92404204e12bdff08b16fb75\",\"463c19427286342120039a83218fa87ce45448e246895abac11fff0036076758\",\"03d287f655813e540ddb9c4e7aeb922478662b0f5d8e9d0cbd564b20146bab76\"],"
                                           "\"20000004\",\"1705c739\",\"64495522\",true]}";

    STRATUM_V1_parse(&stratum_api_v1_message, json_string_abandon_work);
    TEST_ASSERT_EQUAL(MINING_NOTIFY, stratum_api_v1_message.method);
    TEST_ASSERT_TRUE(stratum_api_v1_message.mining_notification->clean_jobs);

    const char *json_string_abandon_work_length_9 = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                                                    "[\"1b4c3d9041\","
                                                    "\"ef4b9a48c7986466de4adc002f7337a6e121bc43000376ea0000000000000000\","
                                                    "\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03a5020cfabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000\","
                                                    "\"41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000\","
                                                    "[\"ae23055e00f0f697cc3640124812d96d4fe8bdfa03484c1c638ce5a1c0e9aa81\",\"980fb87cb61021dd7afd314fcb0dabd096f3d56a7377f6f320684652e7410a21\",\"a52e9868343c55ce405be8971ff340f562ae9ab6353f07140d01666180e19b52\",\"7435bdfa004e603953b2ed39f118803934d9cf17b06d979ceb682f2251bafac2\",\"2a91f061a22d27cb8f44eea79938fb241ebeb359891aa907f05ffde7ed44e52e\",\"302401f80eb5e958155135e25200bb8ea181ad2d05e804a531c7314d86403cdc\",\"318ecb6161eb9b4cfd802bd730e2d36c167ddf102e70aa7b4158e2870dd47392\",\"1114332a9858e0cf84b2425bb1e59eaabf91dd102d114aa443d57fc1b3beb0c9\",\"f43f38095c810613ed795a44d9fab02ff25269706f454885db9be05cdf9c06e1\",\"3e2fc26b27fddc39668b59099cd9635761bb72ed92404204e12bdff08b16fb75\",\"463c19427286342120039a83218fa87ce45448e246895abac11fff0036076758\",\"03d287f655813e540ddb9c4e7aeb922478662b0f5d8e9d0cbd564b20146bab76\"],"
                                                    "\"20000004\",\"1705c739\",\"64495522\",\"64495522\",true]}";

    STRATUM_V1_parse(&stratum_api_v1_message, json_string_abandon_work_length_9);
    TEST_ASSERT_EQUAL(MINING_NOTIFY, stratum_api_v1_message.method);
    TEST_ASSERT_TRUE(stratum_api_v1_message.mining_notification->clean_jobs);
}

TEST_CASE("Parse stratum set_difficulty params", "[mining.set_difficulty]")
{
    const char *json_string = "{\"id\":null,\"method\":\"mining.set_difficulty\",\"params\":[1638]}";
    StratumApiV1Message stratum_api_v1_message = {};
    STRATUM_V1_parse(&stratum_api_v1_message, json_string);
    TEST_ASSERT_EQUAL(MINING_SET_DIFFICULTY, stratum_api_v1_message.method);
    TEST_ASSERT_EQUAL(1638, stratum_api_v1_message.new_difficulty);
}

TEST_CASE("Parse stratum notify params", "[mining.notify]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                              "[\"1d2e0c4d3d\","
                              "\"ef4b9a48c7986466de4adc002f7337a6e121bc43000376ea0000000000000000\","
                              "\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03a5020cfabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000\","
                              "\"41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000\","
                              "[\"ae23055e00f0f697cc3640124812d96d4fe8bdfa03484c1c638ce5a1c0e9aa81\",\"980fb87cb61021dd7afd314fcb0dabd096f3d56a7377f6f320684652e7410a21\",\"a52e9868343c55ce405be8971ff340f562ae9ab6353f07140d01666180e19b52\",\"7435bdfa004e603953b2ed39f118803934d9cf17b06d979ceb682f2251bafac2\",\"2a91f061a22d27cb8f44eea79938fb241ebeb359891aa907f05ffde7ed44e52e\",\"302401f80eb5e958155135e25200bb8ea181ad2d05e804a531c7314d86403cdc\",\"318ecb6161eb9b4cfd802bd730e2d36c167ddf102e70aa7b4158e2870dd47392\",\"1114332a9858e0cf84b2425bb1e59eaabf91dd102d114aa443d57fc1b3beb0c9\",\"f43f38095c810613ed795a44d9fab02ff25269706f454885db9be05cdf9c06e1\",\"3e2fc26b27fddc39668b59099cd9635761bb72ed92404204e12bdff08b16fb75\",\"463c19427286342120039a83218fa87ce45448e246895abac11fff0036076758\",\"03d287f655813e540ddb9c4e7aeb922478662b0f5d8e9d0cbd564b20146bab76\"],"
                              "\"20000004\",\"1705c739\",\"64495522\",false]}";
    STRATUM_V1_parse(&stratum_api_v1_message, json_string);
    TEST_ASSERT_EQUAL_STRING("1d2e0c4d3d", stratum_api_v1_message.mining_notification->job_id);
    TEST_ASSERT_EQUAL_STRING("ef4b9a48c7986466de4adc002f7337a6e121bc43000376ea0000000000000000", stratum_api_v1_message.mining_notification->prev_block_hash);
    TEST_ASSERT_EQUAL_STRING("01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03a5020cfabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000", stratum_api_v1_message.mining_notification->coinbase_1);
    TEST_ASSERT_EQUAL_STRING("41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000", stratum_api_v1_message.mining_notification->coinbase_2);
    TEST_ASSERT_EQUAL_UINT32(0x20000004, stratum_api_v1_message.mining_notification->version);
    TEST_ASSERT_EQUAL_UINT32(0x1705c739, stratum_api_v1_message.mining_notification->target);
    TEST_ASSERT_EQUAL_UINT32(0x64495522, stratum_api_v1_message.mining_notification->ntime);
}

// 'private' function
// TEST_CASE("Test mining.subcribe result parsing", "[mining.subscribe]")
// {
//     const char * json_string = "{\"result\":["
//         "[[\"mining.set_difficulty\",\"731ec5e0649606ff\"],"
//         "[\"mining.notify\",\"731ec5e0649606ff\"]],"
//         "\"e9695791\",4],"
//         "\"id\":1,\"error\":null}";

//     char * extranonce = NULL;
//     int extranonce2_len = 0;
//     int result = _parse_stratum_subscribe_result_message(json_string, &extranonce, &extranonce2_len);
//     TEST_ASSERT_EQUAL(result, 0);
//     TEST_ASSERT_EQUAL_STRING(extranonce, "e9695791");
//     TEST_ASSERT_EQUAL_INT(extranonce2_len, 4);
// }

TEST_CASE("Parse stratum mining.set_version_mask params", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"id\":1,\"method\":\"mining.set_version_mask\",\"params\":[\"1fffe000\"]}";
    STRATUM_V1_parse(&stratum_api_v1_message, json_string);
    TEST_ASSERT_EQUAL(1, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(MINING_SET_VERSION_MASK, stratum_api_v1_message.method);
    TEST_ASSERT_EQUAL_HEX32(0x1fffe000, stratum_api_v1_message.version_mask);
}

TEST_CASE("Parse stratum result success", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_setup_message = {};
    const char* resp1 = "{\"id\":4,\"error\":null,\"result\":true}";
    STRATUM_V1_parse(&stratum_api_v1_setup_message, resp1);
    TEST_ASSERT_EQUAL(4, stratum_api_v1_setup_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT_SETUP, stratum_api_v1_setup_message.method);
    TEST_ASSERT_TRUE(stratum_api_v1_setup_message.response_success);

    StratumApiV1Message stratum_api_v1_message = {};
    const char* json_string = "{\"id\":5,\"error\":null,\"result\":true}";
    STRATUM_V1_parse(&stratum_api_v1_message, json_string);
    TEST_ASSERT_EQUAL(5, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_message.method);
    TEST_ASSERT_TRUE(stratum_api_v1_message.response_success);
}

TEST_CASE("Parse stratum result success with large id", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"id\":32769,\"error\":null,\"result\":true}";
    STRATUM_V1_parse(&stratum_api_v1_message, json_string);
    TEST_ASSERT_EQUAL(32769, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_message.method);
    TEST_ASSERT_TRUE(stratum_api_v1_message.response_success);
}

TEST_CASE("Parse stratum result success with larger id", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"id\":65536,\"error\":null,\"result\":true}";
    STRATUM_V1_parse(&stratum_api_v1_message, json_string);
    TEST_ASSERT_EQUAL(65536, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_message.method);
    TEST_ASSERT_TRUE(stratum_api_v1_message.response_success);
}

TEST_CASE("Parse stratum result error", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_setup_message = {};
    const char* resp1 = "{\"id\":4,\"result\":null,\"error\":[21,\"Job not found\",\"\"]}";
    STRATUM_V1_parse(&stratum_api_v1_setup_message, resp1);
    TEST_ASSERT_EQUAL(4, stratum_api_v1_setup_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT_SETUP, stratum_api_v1_setup_message.method);
    TEST_ASSERT_FALSE(stratum_api_v1_setup_message.response_success);
    TEST_ASSERT_EQUAL_STRING("Job not found", stratum_api_v1_setup_message.error_str);

    StratumApiV1Message stratum_api_v1_message = {};
    const char* json_string = "{\"id\":5,\"result\":null,\"error\":[21,\"Job not found\",\"\"]}";
    STRATUM_V1_parse(&stratum_api_v1_message, json_string);
    TEST_ASSERT_EQUAL(5, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_message.method);
    TEST_ASSERT_FALSE(stratum_api_v1_message.response_success);
    TEST_ASSERT_EQUAL_STRING("Job not found", stratum_api_v1_message.error_str);
}

TEST_CASE("Parse stratum result alternative error", "[stratum]")
{
    StratumApiV1Message stratum_api_v1_message = {};
    const char *json_string = "{\"reject-reason\":\"Above target 2\",\"result\":false,\"error\":null,\"id\":8}";
    STRATUM_V1_parse(&stratum_api_v1_message, json_string);
    TEST_ASSERT_EQUAL(8, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_message.method);
    TEST_ASSERT_FALSE(stratum_api_v1_message.response_success);
    TEST_ASSERT_EQUAL_STRING("Above target 2", stratum_api_v1_message.error_str);
}
