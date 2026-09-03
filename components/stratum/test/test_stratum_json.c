#include "unity.h"
#include "stratum_api.h"
#include <string.h>

static StratumApiV1Message stratum_api_v1_message;
static StratumApiV1Message stratum_api_v1_message2;
static StratumApiV1Message stratum_api_v1_setup_message;
static StratumApiV1Message msg;
static miner_job_t s_test_job;

static bool test_parse(StratumApiV1Message *message, const char *stratum_json)
{
    if (!s_test_job.coinbase_prefix || !s_test_job.coinbase_suffix) {
        miner_job_pool_init();
        s_test_job = *miner_job_get_slot(0);
    }
    return STRATUM_V1_parse(message, stratum_json, &s_test_job);
}
#define STRATUM_V1_parse test_parse

TEST_CASE("Parse stratum method", "[stratum]")
{
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));

    const char *json_string_standard = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                                       "[\"1b4c3d9041\","
                                       "\"ef4b9a48c7986466de4adc002f7337a6e121bc43000376ea0000000000000000\","
                                       "\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03a5020cfabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000\","
                                       "\"41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000\","
                                       "[\"ae23055e00f0f697cc3640124812d96d4fe8bdfa03484c1c638ce5a1c0e9aa81\",\"980fb87cb61021dd7afd314fcb0dabd096f3d56a7377f6f320684652e7410a21\",\"a52e9868343c55ce405be8971ff340f562ae9ab6353f07140d01666180e19b52\",\"7435bdfa004e603953b2ed39f118803934d9cf17b06d979ceb682f2251bafac2\",\"2a91f061a22d27cb8f44eea79938fb241ebeb359891aa907f05ffde7ed44e52e\",\"302401f80eb5e958155135e25200bb8ea181ad2d05e804a531c7314d86403cdc\",\"318ecb6161eb9b4cfd802bd730e2d36c167ddf102e70aa7b4158e2870dd47392\",\"1114332a9858e0cf84b2425bb1e59eaabf91dd102d114aa443d57fc1b3beb0c9\",\"f43f38095c810613ed795a44d9fab02ff25269706f454885db9be05cdf9c06e1\",\"3e2fc26b27fddc39668b59099cd9635761bb72ed92404204e12bdff08b16fb75\",\"463c19427286342120039a83218fa87ce45448e246895abac11fff0036076758\",\"03d287f655813e540ddb9c4e7aeb922478662b0f5d8e9d0cbd564b20146bab76\"],"
                                       "\"20000004\",\"1705c739\",\"64495522\",false]}";

    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string_standard));
    TEST_ASSERT_EQUAL(MINING_NOTIFY, stratum_api_v1_message.method);
    TEST_ASSERT_FALSE(s_test_job.clean_jobs);
}

TEST_CASE("Parse stratum mining.notify abandon work", "[stratum]")
{
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));

    const char *json_string_abandon_work_false = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                                                 "[\"1b4c3d9041\","
                                                 "\"ef4b9a48c7986466de4adc002f7337a6e121bc43000376ea0000000000000000\","
                                                 "\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03a5020cfabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000\","
                                                 "\"41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000\","
                                                 "[\"ae23055e00f0f697cc3640124812d96d4fe8bdfa03484c1c638ce5a1c0e9aa81\",\"980fb87cb61021dd7afd314fcb0dabd096f3d56a7377f6f320684652e7410a21\",\"a52e9868343c55ce405be8971ff340f562ae9ab6353f07140d01666180e19b52\",\"7435bdfa004e603953b2ed39f118803934d9cf17b06d979ceb682f2251bafac2\",\"2a91f061a22d27cb8f44eea79938fb241ebeb359891aa907f05ffde7ed44e52e\",\"302401f80eb5e958155135e25200bb8ea181ad2d05e804a531c7314d86403cdc\",\"318ecb6161eb9b4cfd802bd730e2d36c167ddf102e70aa7b4158e2870dd47392\",\"1114332a9858e0cf84b2425bb1e59eaabf91dd102d114aa443d57fc1b3beb0c9\",\"f43f38095c810613ed795a44d9fab02ff25269706f454885db9be05cdf9c06e1\",\"3e2fc26b27fddc39668b59099cd9635761bb72ed92404204e12bdff08b16fb75\",\"463c19427286342120039a83218fa87ce45448e246895abac11fff0036076758\",\"03d287f655813e540ddb9c4e7aeb922478662b0f5d8e9d0cbd564b20146bab76\"],"
                                                 "\"20000004\",\"1705c739\",\"64495522\",false]}";

    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string_abandon_work_false));
    TEST_ASSERT_EQUAL(MINING_NOTIFY, stratum_api_v1_message.method);
    TEST_ASSERT_FALSE(s_test_job.clean_jobs);

    const char *json_string_abandon_work = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                                           "[\"1b4c3d9041\","
                                           "\"ef4b9a48c7986466de4adc002f7337a6e121bc43000376ea0000000000000000\","
                                           "\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03a5020cfabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000\","
                                           "\"41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000\","
                                           "[\"ae23055e00f0f697cc3640124812d96d4fe8bdfa03484c1c638ce5a1c0e9aa81\",\"980fb87cb61021dd7afd314fcb0dabd096f3d56a7377f6f320684652e7410a21\",\"a52e9868343c55ce405be8971ff340f562ae9ab6353f07140d01666180e19b52\",\"7435bdfa004e603953b2ed39f118803934d9cf17b06d979ceb682f2251bafac2\",\"2a91f061a22d27cb8f44eea79938fb241ebeb359891aa907f05ffde7ed44e52e\",\"302401f80eb5e958155135e25200bb8ea181ad2d05e804a531c7314d86403cdc\",\"318ecb6161eb9b4cfd802bd730e2d36c167ddf102e70aa7b4158e2870dd47392\",\"1114332a9858e0cf84b2425bb1e59eaabf91dd102d114aa443d57fc1b3beb0c9\",\"f43f38095c810613ed795a44d9fab02ff25269706f454885db9be05cdf9c06e1\",\"3e2fc26b27fddc39668b59099cd9635761bb72ed92404204e12bdff08b16fb75\",\"463c19427286342120039a83218fa87ce45448e246895abac11fff0036076758\",\"03d287f655813e540ddb9c4e7aeb922478662b0f5d8e9d0cbd564b20146bab76\"],"
                                           "\"20000004\",\"1705c739\",\"64495522\",true]}";

    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string_abandon_work));
    TEST_ASSERT_EQUAL(MINING_NOTIFY, stratum_api_v1_message.method);
    TEST_ASSERT_TRUE(s_test_job.clean_jobs);

    const char *json_string_abandon_work_length_9 = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                                                    "[\"1b4c3d9041\","
                                                    "\"ef4b9a48c7986466de4adc002f7337a6e121bc43000376ea0000000000000000\","
                                                    "\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03a5020cfabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000\","
                                                    "\"41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000\","
                                                    "[\"ae23055e00f0f697cc3640124812d96d4fe8bdfa03484c1c638ce5a1c0e9aa81\",\"980fb87cb61021dd7afd314fcb0dabd096f3d56a7377f6f320684652e7410a21\",\"a52e9868343c55ce405be8971ff340f562ae9ab6353f07140d01666180e19b52\",\"7435bdfa004e603953b2ed39f118803934d9cf17b06d979ceb682f2251bafac2\",\"2a91f061a22d27cb8f44eea79938fb241ebeb359891aa907f05ffde7ed44e52e\",\"302401f80eb5e958155135e25200bb8ea181ad2d05e804a531c7314d86403cdc\",\"318ecb6161eb9b4cfd802bd730e2d36c167ddf102e70aa7b4158e2870dd47392\",\"1114332a9858e0cf84b2425bb1e59eaabf91dd102d114aa443d57fc1b3beb0c9\",\"f43f38095c810613ed795a44d9fab02ff25269706f454885db9be05cdf9c06e1\",\"3e2fc26b27fddc39668b59099cd9635761bb72ed92404204e12bdff08b16fb75\",\"463c19427286342120039a83218fa87ce45448e246895abac11fff0036076758\",\"03d287f655813e540ddb9c4e7aeb922478662b0f5d8e9d0cbd564b20146bab76\"],"
                                                    "\"20000004\",\"1705c739\",\"64495522\",\"64495522\",true]}";

    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string_abandon_work_length_9));
    TEST_ASSERT_EQUAL(MINING_NOTIFY, stratum_api_v1_message.method);
    TEST_ASSERT_TRUE(s_test_job.clean_jobs);
}

TEST_CASE("Parse stratum set_difficulty params", "[mining.set_difficulty]")
{
    const char *json_string = "{\"id\":null,\"method\":\"mining.set_difficulty\",\"params\":[1638]}";
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(MINING_SET_DIFFICULTY, stratum_api_v1_message.method);
    TEST_ASSERT_EQUAL_DOUBLE(1638.0, stratum_api_v1_message.new_difficulty);
}

TEST_CASE("Parse stratum set_difficulty params with fractional", "[mining.set_difficulty]")
{
    const char *json_string = "{\"id\":null,\"method\":\"mining.set_difficulty\",\"params\":[100.5]}";
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(MINING_SET_DIFFICULTY, stratum_api_v1_message.method);
    TEST_ASSERT_EQUAL_DOUBLE(100.5, stratum_api_v1_message.new_difficulty);
}

TEST_CASE("Parse stratum notify params", "[mining.notify]")
{
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    const char *json_string = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                              "[\"1d2e0c4d3d\","
                              "\"ef4b9a48c7986466de4adc002f7337a6e121bc43000376ea0000000000000000\","
                              "\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03a5020cfabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000\","
                              "\"41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000\","
                              "[\"ae23055e00f0f697cc3640124812d96d4fe8bdfa03484c1c638ce5a1c0e9aa81\",\"980fb87cb61021dd7afd314fcb0dabd096f3d56a7377f6f320684652e7410a21\",\"a52e9868343c55ce405be8971ff340f562ae9ab6353f07140d01666180e19b52\",\"7435bdfa004e603953b2ed39f118803934d9cf17b06d979ceb682f2251bafac2\",\"2a91f061a22d27cb8f44eea79938fb241ebeb359891aa907f05ffde7ed44e52e\",\"302401f80eb5e958155135e25200bb8ea181ad2d05e804a531c7314d86403cdc\",\"318ecb6161eb9b4cfd802bd730e2d36c167ddf102e70aa7b4158e2870dd47392\",\"1114332a9858e0cf84b2425bb1e59eaabf91dd102d114aa443d57fc1b3beb0c9\",\"f43f38095c810613ed795a44d9fab02ff25269706f454885db9be05cdf9c06e1\",\"3e2fc26b27fddc39668b59099cd9635761bb72ed92404204e12bdff08b16fb75\",\"463c19427286342120039a83218fa87ce45448e246895abac11fff0036076758\",\"03d287f655813e540ddb9c4e7aeb922478662b0f5d8e9d0cbd564b20146bab76\"],"
                              "\"20000004\",\"1705c739\",\"64495522\",false]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL_STRING("1d2e0c4d3d", s_test_job.job_id);
    TEST_ASSERT_EQUAL_UINT32(0x20000004, s_test_job.version);
    TEST_ASSERT_EQUAL_UINT32(0x1705c739, s_test_job.nbits);
    TEST_ASSERT_EQUAL_UINT32(0x64495522, s_test_job.ntime);
    TEST_ASSERT_EQUAL(12, s_test_job.merkle_path_count);
    TEST_ASSERT_EQUAL(JOB_TYPE_V1, s_test_job.type);
}

TEST_CASE("Test mining.subcribe result parsing", "[mining.subscribe]")
{
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    const char * json_string = "{\"result\":[[[\"mining.notify\",\"695482c0\"]],\"4de05269\",8],\"id\":2,\"error\":null}";

    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL_STRING("4de05269", stratum_api_v1_message.extranonce_str);
    TEST_ASSERT_EQUAL_INT(8, stratum_api_v1_message.extranonce_2_len);
}

TEST_CASE("Parse stratum mining.subscribe result malformed", "[mining.subscribe]")
{
    // Only 2 array items — extranonce2_len is missing
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    const char *json_string = "{\"result\":[[[\"mining.notify\",\"abc\"]],\"4de05269\"],\"id\":2,\"error\":null}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
}

TEST_CASE("Parse stratum mining.set_version_mask params", "[stratum]")
{
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    const char *json_string = "{\"id\":1,\"method\":\"mining.set_version_mask\",\"params\":[\"1fffe000\"]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(1, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(MINING_SET_VERSION_MASK, stratum_api_v1_message.method);
    TEST_ASSERT_EQUAL_HEX32(0x1fffe000, stratum_api_v1_message.version_mask);
}

TEST_CASE("Parse stratum result success", "[stratum]")
{
    memset(&stratum_api_v1_setup_message, 0, sizeof(stratum_api_v1_setup_message));
    const char* resp1 = "{\"id\":4,\"error\":null,\"result\":true}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_setup_message, resp1));
    TEST_ASSERT_EQUAL(4, stratum_api_v1_setup_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_setup_message.method);
    TEST_ASSERT_TRUE(stratum_api_v1_setup_message.response_success);

    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    const char* json_string = "{\"id\":5,\"error\":null,\"result\":true}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(5, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_message.method);
    TEST_ASSERT_TRUE(stratum_api_v1_message.response_success);
}

TEST_CASE("Parse stratum result success with large id", "[stratum]")
{
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    const char *json_string = "{\"id\":32769,\"error\":null,\"result\":true}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(32769, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_message.method);
    TEST_ASSERT_TRUE(stratum_api_v1_message.response_success);
}

TEST_CASE("Parse stratum result success with larger id", "[stratum]")
{
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    const char *json_string = "{\"id\":65536,\"error\":null,\"result\":true}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(65536, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_message.method);
    TEST_ASSERT_TRUE(stratum_api_v1_message.response_success);
}

TEST_CASE("Parse stratum result error", "[stratum]")
{
    memset(&stratum_api_v1_setup_message, 0, sizeof(stratum_api_v1_setup_message));
    const char* resp1 = "{\"id\":4,\"result\":null,\"error\":[21,\"Job not found\",\"\"]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_setup_message, resp1));
    TEST_ASSERT_EQUAL(4, stratum_api_v1_setup_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_setup_message.method);
    TEST_ASSERT_FALSE(stratum_api_v1_setup_message.response_success);
    TEST_ASSERT_EQUAL_STRING("Job not found", stratum_api_v1_setup_message.error_str);

    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    const char* json_string = "{\"id\":5,\"result\":null,\"error\":[21,\"Job not found\",\"\"]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(5, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_message.method);
    TEST_ASSERT_FALSE(stratum_api_v1_message.response_success);
    TEST_ASSERT_EQUAL_STRING("Job not found", stratum_api_v1_message.error_str);
}

TEST_CASE("Parse stratum result alternative error", "[stratum]")
{
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    const char *json_string = "{\"reject-reason\":\"Above target 2\",\"result\":false,\"error\":null,\"id\":8}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(8, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_message.method);
    TEST_ASSERT_FALSE(stratum_api_v1_message.response_success);
    TEST_ASSERT_EQUAL_STRING("Above target 2", stratum_api_v1_message.error_str);
}

TEST_CASE("Parse stratum result with error string (Stale)", "[stratum]")
{
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    const char *json_string = "{\"result\":false,\"error\":\"Stale\",\"id\":618}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(618, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_message.method);
    TEST_ASSERT_FALSE(stratum_api_v1_message.response_success);
    TEST_ASSERT_EQUAL_STRING("Stale", stratum_api_v1_message.error_str);
}

TEST_CASE("Parse stratum result with null result and error string", "[stratum]")
{
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    const char *json_string = "{\"result\":null,\"error\":\"Stale\",\"id\":618}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(618, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_message.method);
    TEST_ASSERT_FALSE(stratum_api_v1_message.response_success);
    TEST_ASSERT_EQUAL_STRING("Stale", stratum_api_v1_message.error_str);
}

TEST_CASE("Parse stratum error array format", "[stratum]")
{
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    const char *json_string = "{\"id\":50,\"result\":null,\"error\":[21,\"Job not found\",\"\"]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(50, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_message.method);
    TEST_ASSERT_FALSE(stratum_api_v1_message.response_success);
    TEST_ASSERT_EQUAL_STRING("Job not found", stratum_api_v1_message.error_str);
}

TEST_CASE("Parse stratum error jsonrpc object with code", "[stratum]")
{
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    const char *json_string = "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":22,\"message\":\"duplicate share\",\"data\":null},\"id\":42}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(42, stratum_api_v1_message.message_id);
    TEST_ASSERT_EQUAL(STRATUM_RESULT, stratum_api_v1_message.method);
    TEST_ASSERT_FALSE(stratum_api_v1_message.response_success);
    TEST_ASSERT_EQUAL_STRING("duplicate share", stratum_api_v1_message.error_str);
}

TEST_CASE("Parse stratum invalid json or malformed parameters", "[stratum]")
{
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    const char *json_string = "{\"id\":null,\"method\":\"mining.notify\",\"params\":[]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));

    memset(&stratum_api_v1_message2, 0, sizeof(stratum_api_v1_message2));
    const char *json_string2 = "invalid json";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&stratum_api_v1_message2, json_string2));
}

TEST_CASE("Parse stratum mining.set_extranonce params", "[stratum]")
{
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    const char *json_string = "{\"id\":1,\"method\":\"mining.set_extranonce\",\"params\":[\"deadbeef\",8]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(MINING_SET_EXTRANONCE, stratum_api_v1_message.method);
    TEST_ASSERT_EQUAL_STRING("deadbeef", stratum_api_v1_message.extranonce_str);
    TEST_ASSERT_EQUAL_INT(8, stratum_api_v1_message.extranonce_2_len);
}

TEST_CASE("Parse stratum mining.set_extranonce invalid params", "[stratum]")
{
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    const char *json_string = "{\"id\":1,\"method\":\"mining.set_extranonce\",\"params\":[]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
}

TEST_CASE("Parse stratum client.show_message", "[stratum]")
{
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    const char *json_string = "{\"id\":null,\"method\":\"client.show_message\",\"params\":[\"Welcome to the pool!\"]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(CLIENT_SHOW_MESSAGE, stratum_api_v1_message.method);
    TEST_ASSERT_EQUAL_STRING("Welcome to the pool!", stratum_api_v1_message.show_message);
}

TEST_CASE("Parse stratum client.show_message invalid params", "[stratum]")
{
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    const char *json_string = "{\"id\":null,\"method\":\"client.show_message\",\"params\":[]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
}

TEST_CASE("Parse stratum client.get_version", "[stratum]")
{
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    const char *json_string = "{\"id\":10,\"method\":\"client.get_version\",\"params\":[]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(CLIENT_GET_VERSION, stratum_api_v1_message.method);
    TEST_ASSERT_EQUAL_STRING("unknown", stratum_api_v1_message.version_string);
}

TEST_CASE("Parse stratum client.reconnect", "[stratum]")
{
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    const char *json_string = "{\"id\":null,\"method\":\"client.reconnect\",\"params\":[]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(CLIENT_RECONNECT, stratum_api_v1_message.method);
}

TEST_CASE("Parse stratum mining.ping", "[stratum]")
{
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    const char *json_string = "{\"id\":null,\"method\":\"mining.ping\",\"params\":[]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(MINING_PING, stratum_api_v1_message.method);
}

TEST_CASE("Parse stratum unknown method returns false", "[stratum]")
{
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    const char *json_string = "{\"id\":null,\"method\":\"mining.hashrate\",\"params\":[]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
}

TEST_CASE("Parse stratum configure result", "[stratum]")
{
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    const char *json_string = "{\"id\":1,\"result\":{\"version-rolling\":true,\"version-rolling.mask\":\"1fffe000\"},\"error\":null}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(STRATUM_RESULT_CONFIGURE, stratum_api_v1_message.method);
    TEST_ASSERT_TRUE(stratum_api_v1_message.response_success);
    TEST_ASSERT_EQUAL_HEX32(0x1fffe000, stratum_api_v1_message.version_mask);
}

TEST_CASE("Parse stratum set_difficulty rejects invalid values", "[mining.set_difficulty]")
{
    memset(&msg, 0, sizeof(msg));

    // Negative difficulty
    const char *json_neg = "{\"id\":null,\"method\":\"mining.set_difficulty\",\"params\":[-10.0]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&msg, json_neg));

    // Zero difficulty
    const char *json_zero = "{\"id\":null,\"method\":\"mining.set_difficulty\",\"params\":[0.0]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&msg, json_zero));

    // Extremely small / subnormal difficulty
    const char *json_tiny = "{\"id\":null,\"method\":\"mining.set_difficulty\",\"params\":[0.000001]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&msg, json_tiny));

    // Extremely large difficulty (overflow)
    const char *json_huge = "{\"id\":null,\"method\":\"mining.set_difficulty\",\"params\":[1e20]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&msg, json_huge));
}

TEST_CASE("Parse stratum mining.set_extranonce negative length clamped", "[stratum]")
{
    memset(&msg, 0, sizeof(msg));
    const char *json_neg_e2 = "{\"id\":1,\"method\":\"mining.set_extranonce\",\"params\":[\"deadbeef\",-1]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&msg, json_neg_e2));
    TEST_ASSERT_EQUAL(MINING_SET_EXTRANONCE, msg.method);
    TEST_ASSERT_EQUAL_INT(0, msg.extranonce_2_len);

    const char *json_oversized_e2 = "{\"id\":1,\"method\":\"mining.set_extranonce\",\"params\":[\"deadbeef\",64]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&msg, json_oversized_e2));
    TEST_ASSERT_EQUAL_INT(32, msg.extranonce_2_len);

    // Odd hex string length should be rejected
    const char *json_odd_hex = "{\"id\":1,\"method\":\"mining.set_extranonce\",\"params\":[\"deadbee\",8]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&msg, json_odd_hex));
}

TEST_CASE("Parse stratum mining.notify hardening", "[mining.notify]")
{
    memset(&msg, 0, sizeof(msg));

    // Short prev_hash (not 64 hex chars)
    const char *json_short_hash = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                                  "[\"1\",\"deadbeef\",\"0100\",\"0200\",[],\"20000000\",\"1705ae3a\",\"647025b5\",true]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&msg, json_short_hash));

    // Pre-genesis ntime (< 1231006505)
    const char *json_bad_ntime = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                                 "[\"1\",\"0000000000000000000000000000000000000000000000000000000000000000\",\"0100\",\"0200\",[],\"20000000\",\"1705ae3a\",\"00000001\",true]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&msg, json_bad_ntime));

    // Empty job_id
    const char *json_empty_job = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                                 "[\"\",\"0000000000000000000000000000000000000000000000000000000000000000\",\"0100\",\"0200\",[],\"20000000\",\"1705ae3a\",\"647025b5\",true]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&msg, json_empty_job));
}

TEST_CASE("Parse stratum notify type confusion", "[mining.notify]")
{
    memset(&msg, 0, sizeof(msg));

    // Integer job_id
    const char *json_int_job = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                               "[12345,\"0000000000000000000000000000000000000000000000000000000000000000\",\"0100\",\"0200\",[],\"20000000\",\"1705ae3a\",\"647025b5\",true]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&msg, json_int_job));

    // Integer prev_hash
    const char *json_int_prevhash = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                                    "[\"1\",123456789,\"0100\",\"0200\",[],\"20000000\",\"1705ae3a\",\"647025b5\",true]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&msg, json_int_prevhash));

    // Integer coinbase_1
    const char *json_int_c1 = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                              "[\"1\",\"0000000000000000000000000000000000000000000000000000000000000000\",100,\"0200\",[],\"20000000\",\"1705ae3a\",\"647025b5\",true]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&msg, json_int_c1));

    // Non-array merkle_branch (string)
    const char *json_str_merkle = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                                  "[\"1\",\"0000000000000000000000000000000000000000000000000000000000000000\",\"0100\",\"0200\",\"invalid_merkle\",\"20000000\",\"1705ae3a\",\"647025b5\",true]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&msg, json_str_merkle));

    // Merkle branch containing integers
    const char *json_int_in_merkle = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                                     "[\"1\",\"0000000000000000000000000000000000000000000000000000000000000000\",\"0100\",\"0200\",[12345],\"20000000\",\"1705ae3a\",\"647025b5\",true]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&msg, json_int_in_merkle));

    // Integer version
    const char *json_int_ver = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                               "[\"1\",\"0000000000000000000000000000000000000000000000000000000000000000\",\"0100\",\"0200\",[],536870912,\"1705ae3a\",\"647025b5\",true]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&msg, json_int_ver));
}

TEST_CASE("Parse stratum subscribe result extranonce negative size", "[mining.subscribe]")
{
    memset(&msg, 0, sizeof(msg));
    const char *json_sub_neg = "{\"result\":[[[\"mining.notify\",\"695482c0\"]],\"4de05269\",-1],\"id\":2,\"error\":null}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&msg, json_sub_neg));
    TEST_ASSERT_EQUAL_STRING("4de05269", msg.extranonce_str);
    TEST_ASSERT_EQUAL_INT(0, msg.extranonce_2_len);

    const char *json_sub_oversized = "{\"result\":[[[\"mining.notify\",\"695482c0\"]],\"4de05269\",100],\"id\":2,\"error\":null}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&msg, json_sub_oversized));
    TEST_ASSERT_EQUAL_INT(32, msg.extranonce_2_len);

    // Odd length extranonce1 in subscribe result should be rejected
    const char *json_sub_odd_e1 = "{\"result\":[[[\"mining.notify\",\"695482c0\"]],\"4de0526\",4],\"id\":2,\"error\":null}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&msg, json_sub_odd_e1));

    // Oversized extranonce1 (> 64 hex chars) should be rejected
    const char *json_sub_huge_e1 = "{\"result\":[[[\"mining.notify\",\"695482c0\"]],\"00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff0011\",4],\"id\":2,\"error\":null}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&msg, json_sub_huge_e1));
}

TEST_CASE("Parse stratum version mask BIP320 clamping", "[stratum]")
{
    memset(&msg, 0, sizeof(msg));

    // Mask with bits outside BIP320 (e.g. 0xffffffff) should be clamped to 0x1fffe000
    const char *json_set_mask = "{\"id\":null,\"method\":\"mining.set_version_mask\",\"params\":[\"ffffffff\"]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&msg, json_set_mask));
    TEST_ASSERT_EQUAL(MINING_SET_VERSION_MASK, msg.method);
    TEST_ASSERT_EQUAL_HEX32(0x1fffe000, msg.version_mask);

    // Configure result with full mask should also clamp
    const char *json_cfg_mask = "{\"id\":1,\"result\":{\"version-rolling\":true,\"version-rolling.mask\":\"ffffffff\"},\"error\":null}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&msg, json_cfg_mask));
    TEST_ASSERT_EQUAL(STRATUM_RESULT_CONFIGURE, msg.method);
    TEST_ASSERT_EQUAL_HEX32(0x1fffe000, msg.version_mask);
}

TEST_CASE("Parse stratum mining.notify version, nbits, ntime length validation", "[mining.notify]")
{
    memset(&msg, 0, sizeof(msg));

    // Short version (e.g. "2000000" - 7 chars instead of 8)
    const char *json_short_ver = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                                 "[\"1\",\"0000000000000000000000000000000000000000000000000000000000000000\",\"0100\",\"0200\",[],\"2000000\",\"1705ae3a\",\"647025b5\",true]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&msg, json_short_ver));

    // Short nbits (e.g. "1705ae" - 6 chars instead of 8)
    const char *json_short_nbits = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                                   "[\"1\",\"0000000000000000000000000000000000000000000000000000000000000000\",\"0100\",\"0200\",[],\"20000000\",\"1705ae\",\"647025b5\",true]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&msg, json_short_nbits));

    // Short ntime (e.g. "647025" - 6 chars instead of 8)
    const char *json_short_ntime = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                                   "[\"1\",\"0000000000000000000000000000000000000000000000000000000000000000\",\"0100\",\"0200\",[],\"20000000\",\"1705ae3a\",\"647025\",true]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&msg, json_short_ntime));

    // Valid 8-char version, nbits, ntime
    const char *json_valid = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                             "[\"1\",\"0000000000000000000000000000000000000000000000000000000000000000\",\"0100\",\"0200\",[],\"20000000\",\"1705ae3a\",\"647025b5\",true]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&msg, json_valid));
    TEST_ASSERT_EQUAL(MINING_NOTIFY, msg.method);
    TEST_ASSERT_EQUAL_HEX32(0x20000000, s_test_job.version);
    TEST_ASSERT_EQUAL_HEX32(0x1705ae3a, s_test_job.nbits);
    TEST_ASSERT_EQUAL_HEX32(0x647025b5, s_test_job.ntime);
}

TEST_CASE("Parse stratum mining.notify job_id length validation", "[mining.notify]")
{
    memset(&msg, 0, sizeof(msg));

    // 32-char job_id exceeds sizeof(job->job_id) - 1 (31) and must be rejected
    const char *json_long_job_id = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                                   "[\"12345678901234567890123456789012\",\"0000000000000000000000000000000000000000000000000000000000000000\",\"0100\",\"0200\",[],\"20000000\",\"1705ae3a\",\"647025b5\",true]}";
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&msg, json_long_job_id));

    // 31-char job_id fits perfectly
    const char *json_valid_job_id = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                                    "[\"1234567890123456789012345678901\",\"0000000000000000000000000000000000000000000000000000000000000000\",\"0100\",\"0200\",[],\"20000000\",\"1705ae3a\",\"647025b5\",true]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&msg, json_valid_job_id));
    TEST_ASSERT_EQUAL_STRING("1234567890123456789012345678901", s_test_job.job_id);
}

TEST_CASE("Parse stratum mining.notify large coinbase suffix (multi-payout pool)", "[mining.notify]")
{
    memset(&msg, 0, sizeof(msg));

    // Construct a 2000-hex-char (1000 byte) coinbase_2 suffix representing multi-output pool
    static char c2_hex[2001];
    memset(c2_hex, 'a', 2000);
    c2_hex[2000] = '\0';

    static char json_buf[2500];
    snprintf(json_buf, sizeof(json_buf),
             "{\"id\":null,\"method\":\"mining.notify\",\"params\":[\"job1\",\"0000000000000000000000000000000000000000000000000000000000000000\",\"0100\",\"%s\",[],\"20000000\",\"1705ae3a\",\"647025b5\",true]}",
             c2_hex);

    TEST_ASSERT_TRUE(STRATUM_V1_parse(&msg, json_buf));
    TEST_ASSERT_EQUAL(1000, s_test_job.coinbase_suffix_len);
    TEST_ASSERT_EQUAL_UINT8(0xaa, s_test_job.coinbase_suffix[0]);
    TEST_ASSERT_EQUAL_UINT8(0xaa, s_test_job.coinbase_suffix[999]);
}
