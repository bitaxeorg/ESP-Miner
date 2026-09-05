#include "unity.h"
#include "stratum_protocol.h"
#include "sv1_protocol.h"
#include "utils.h"

static StratumApiV1Message stratum_api_v1_message;
static StratumApiV1Message stratum_api_v1_message2;
static StratumApiV1Message stratum_api_v1_setup_message;
static StratumApiV1Message msg;
static miner_job_t s_test_job;

static bool parse_with_job(StratumApiV1Message *message, const char *stratum_json,
                           miner_job_t *job)
{
    return STRATUM_V1_parse(message, stratum_json, job);
}

static bool test_parse(StratumApiV1Message *message, const char *stratum_json)
{
    if (!s_test_job.coinbase_prefix || !s_test_job.coinbase_suffix) {
        miner_job_pool_init();
        s_test_job = *miner_job_get_slot(0);
    }
    return STRATUM_V1_parse(message, stratum_json, &s_test_job);
}
#define STRATUM_V1_parse test_parse

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VALID_PREVIOUS_BLOCK_HASH "0000000000000000000000000000000000000000000000000000000000000000"
#define VALID_MERKLE_BRANCH "0000000000000000000000000000000000000000000000000000000000000000"

static char *create_notify_message(const char *coinbase_prefix, const char *coinbase_suffix,
                                   size_t merkle_branch_count, const char *merkle_branch,
                                   const char *ntime)
{
    size_t branch_list_capacity = 3 + merkle_branch_count * (strlen(merkle_branch) + 3);
    char *branch_list = malloc(branch_list_capacity);
    if (branch_list == NULL) {
        return NULL;
    }

    size_t offset = 0;
    offset += (size_t)snprintf(branch_list + offset, branch_list_capacity - offset, "[");
    for (size_t index = 0; index < merkle_branch_count; ++index) {
        offset += (size_t)snprintf(branch_list + offset, branch_list_capacity - offset,
                                   "%s\"%s\"", index == 0 ? "" : ",", merkle_branch);
    }
    (void)snprintf(branch_list + offset, branch_list_capacity - offset, "]");

    size_t json_capacity = strlen(coinbase_prefix) + strlen(coinbase_suffix) +
                           strlen(branch_list) + strlen(ntime) + 256;
    char *json = malloc(json_capacity);
    if (json == NULL) {
        free(branch_list);
        return NULL;
    }

    (void)snprintf(
        json, json_capacity,
        "{\"id\":null,\"method\":\"mining.notify\",\"params\":[\"job-1\",\"%s\",\"%s\",\"%s\",%s,\"20000000\",\"1705ae3a\",\"%s\",true]}",
        VALID_PREVIOUS_BLOCK_HASH, coinbase_prefix, coinbase_suffix, branch_list, ntime);
    free(branch_list);
    return json;
}

static bool parse_notify(StratumApiV1Message *message, const char *coinbase_prefix,
                         const char *coinbase_suffix, size_t merkle_branch_count,
                         const char *merkle_branch, const char *ntime)
{
    char *json = create_notify_message(coinbase_prefix, coinbase_suffix,
                                       merkle_branch_count, merkle_branch, ntime);
    if (json == NULL) {
        return false;
    }
    bool result = STRATUM_V1_parse(message, json);
    free(json);
    return result;
}

static bool parse_notify_fields(StratumApiV1Message *message, const char *job_id,
                                const char *previous_hash, const char *coinbase_prefix,
                                const char *coinbase_suffix, const char *merkle_branches,
                                const char *version, const char *nbits, const char *ntime,
                                const char *clean_jobs)
{
    size_t json_capacity = strlen(job_id) + strlen(previous_hash) + strlen(coinbase_prefix) +
                           strlen(coinbase_suffix) + strlen(merkle_branches) + strlen(version) +
                           strlen(nbits) + strlen(ntime) + strlen(clean_jobs) + 128;
    char *json = malloc(json_capacity);
    if (json == NULL) return false;

    (void)snprintf(
        json, json_capacity,
        "{\"id\":null,\"method\":\"mining.notify\",\"params\":[\"%s\",\"%s\",\"%s\",\"%s\",%s,\"%s\",\"%s\",\"%s\",%s]}",
        job_id, previous_hash, coinbase_prefix, coinbase_suffix, merkle_branches,
        version, nbits, ntime, clean_jobs);
    bool result = STRATUM_V1_parse(message, json);
    free(json);
    return result;
}

TEST_CASE("Stratum protocol identifiers convert to and from strings", "[stratum protocol]")
{
    TEST_ASSERT_EQUAL_INT(STRATUM_PROTOCOL_V1, stratum_protocol_from_string(STRATUM_V1));
    TEST_ASSERT_EQUAL_INT(STRATUM_PROTOCOL_V2, stratum_protocol_from_string(STRATUM_V2));
    TEST_ASSERT_EQUAL_INT(STRATUM_PROTOCOL_UNKNOWN, stratum_protocol_from_string("invalid"));
    TEST_ASSERT_EQUAL_INT(STRATUM_PROTOCOL_UNKNOWN, stratum_protocol_from_string(NULL));

    TEST_ASSERT_EQUAL_STRING(STRATUM_V1, stratum_protocol_to_string(STRATUM_PROTOCOL_V1));
    TEST_ASSERT_EQUAL_STRING(STRATUM_V2, stratum_protocol_to_string(STRATUM_PROTOCOL_V2));
    TEST_ASSERT_EQUAL_STRING("unknown", stratum_protocol_to_string(STRATUM_PROTOCOL_UNKNOWN));
}

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

    const char *json_string_abandon_work_with_extension = "{\"id\":null,\"method\":\"mining.notify\",\"params\":"
                                                    "[\"1b4c3d9041\","
                                                    "\"ef4b9a48c7986466de4adc002f7337a6e121bc43000376ea0000000000000000\","
                                                    "\"01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff4b03a5020cfabe6d6d379ae882651f6469f2ed6b8b40a4f9a4b41fd838a3ad6de8cba775f4e8f1d3080100000000000000\","
                                                    "\"41903d4c1b2f736c7573682f0000000003ca890d27000000001976a9147c154ed1dc59609e3d26abb2df2ea3d587cd8c4188ac00000000000000002c6a4c2952534b424c4f434b3a4cb4cb2ddfc37c41baf5ef6b6b4899e3253a8f1dfc7e5dd68a5b5b27005014ef0000000000000000266a24aa21a9ed5caa249f1af9fbf71c986fea8e076ca34ae3514fb2f86400561b28c7b15949bf00000000\","
                                                    "[\"ae23055e00f0f697cc3640124812d96d4fe8bdfa03484c1c638ce5a1c0e9aa81\",\"980fb87cb61021dd7afd314fcb0dabd096f3d56a7377f6f320684652e7410a21\",\"a52e9868343c55ce405be8971ff340f562ae9ab6353f07140d01666180e19b52\",\"7435bdfa004e603953b2ed39f118803934d9cf17b06d979ceb682f2251bafac2\",\"2a91f061a22d27cb8f44eea79938fb241ebeb359891aa907f05ffde7ed44e52e\",\"302401f80eb5e958155135e25200bb8ea181ad2d05e804a531c7314d86403cdc\",\"318ecb6161eb9b4cfd802bd730e2d36c167ddf102e70aa7b4158e2870dd47392\",\"1114332a9858e0cf84b2425bb1e59eaabf91dd102d114aa443d57fc1b3beb0c9\",\"f43f38095c810613ed795a44d9fab02ff25269706f454885db9be05cdf9c06e1\",\"3e2fc26b27fddc39668b59099cd9635761bb72ed92404204e12bdff08b16fb75\",\"463c19427286342120039a83218fa87ce45448e246895abac11fff0036076758\",\"03d287f655813e540ddb9c4e7aeb922478662b0f5d8e9d0cbd564b20146bab76\"],"
                                                    "\"20000004\",\"1705c739\",\"64495522\",true,\"extension\"]}";

    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string_abandon_work_with_extension));
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
    STRATUM_V1_reset_message(&stratum_api_v1_message);
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
    STRATUM_V1_reset_message(&stratum_api_v1_setup_message);
    STRATUM_V1_reset_message(&stratum_api_v1_message);
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
    STRATUM_V1_reset_message(&stratum_api_v1_message);
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
    STRATUM_V1_reset_message(&stratum_api_v1_message);
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
    STRATUM_V1_reset_message(&stratum_api_v1_message);
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
    STRATUM_V1_reset_message(&stratum_api_v1_message);
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
    STRATUM_V1_reset_message(&stratum_api_v1_message);
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

TEST_CASE("Parse stratum rejects invalid field types", "[stratum]")
{
    const char *invalid_messages[] = {
        "{\"id\":null,\"method\":\"mining.set_difficulty\",\"params\":[]}",
        "{\"id\":null,\"method\":\"mining.set_difficulty\",\"params\":[\"high\"]}",
        "{\"id\":null,\"method\":\"mining.set_version_mask\",\"params\":[]}",
        "{\"id\":null,\"method\":\"mining.set_version_mask\",\"params\":[123]}",
        "{\"id\":null,\"method\":\"mining.set_extranonce\",\"params\":[123,4]}",
        "{\"id\":null,\"method\":\"client.show_message\",\"params\":[123]}",
        "{\"id\":2,\"result\":[[],123,4],\"error\":null}",
        "{\"id\":1,\"result\":{\"version-rolling\":false,\"version-rolling.mask\":\"1fffe000\"},\"error\":null}",
    };

    for (size_t index = 0; index < sizeof(invalid_messages) / sizeof(invalid_messages[0]); ++index) {
        StratumApiV1Message message = {};
        TEST_ASSERT_FALSE(STRATUM_V1_parse(&message, invalid_messages[index]));
        STRATUM_V1_reset_message(&message);
    }
}

TEST_CASE("Parse stratum mining.set_extranonce params", "[stratum]")
{
    memset(&stratum_api_v1_message, 0, sizeof(stratum_api_v1_message));
    const char *json_string = "{\"id\":1,\"method\":\"mining.set_extranonce\",\"params\":[\"deadbeef\",8]}";
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&stratum_api_v1_message, json_string));
    TEST_ASSERT_EQUAL(MINING_SET_EXTRANONCE, stratum_api_v1_message.method);
    TEST_ASSERT_EQUAL_STRING("deadbeef", stratum_api_v1_message.extranonce_str);
    TEST_ASSERT_EQUAL_INT(8, stratum_api_v1_message.extranonce_2_len);
    STRATUM_V1_reset_message(&stratum_api_v1_message);
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
    STRATUM_V1_reset_message(&stratum_api_v1_message);
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
    STRATUM_V1_reset_message(&stratum_api_v1_message);
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

TEST_CASE("Parse stratum mining.notify requires an array of parameters", "[mining.notify]")
{
    StratumApiV1Message message = {};

    TEST_ASSERT_FALSE(STRATUM_V1_parse(
        &message, "{\"id\":null,\"method\":\"mining.notify\"}"));
    TEST_ASSERT_FALSE(STRATUM_V1_parse(
        &message, "{\"id\":null,\"method\":\"mining.notify\",\"params\":{}}"));
}

TEST_CASE("Parse stratum mining.notify rejects invalid coinbase lengths", "[mining.notify]")
{
    StratumApiV1Message message = {};

    TEST_ASSERT_FALSE(parse_notify(
        &message, "", "0200", 0, VALID_MERKLE_BRANCH, "647025b5"));
    TEST_ASSERT_FALSE(parse_notify(
        &message, "0", "0200", 0, VALID_MERKLE_BRANCH, "647025b5"));
    TEST_ASSERT_FALSE(parse_notify(
        &message, "0100", "0", 0, VALID_MERKLE_BRANCH, "647025b5"));
}

TEST_CASE("Parse stratum mining.notify enforces coinbase storage", "[mining.notify][not-on-qemu]")
{
    StratumApiV1Message message = {};
    size_t maximum_prefix_chars = MAX_COINBASE_PREFIX_LEN * 2;
    size_t maximum_suffix_chars = MAX_COINBASE_SUFFIX_LEN * 2;
    char *maximum_prefix = malloc(maximum_prefix_chars + 1);
    char *maximum_suffix = malloc(maximum_suffix_chars + 1);
    char *oversized_prefix = malloc(maximum_prefix_chars + 3);
    char *oversized_suffix = malloc(maximum_suffix_chars + 3);
    TEST_ASSERT_NOT_NULL(maximum_prefix);
    TEST_ASSERT_NOT_NULL(maximum_suffix);
    TEST_ASSERT_NOT_NULL(oversized_prefix);
    TEST_ASSERT_NOT_NULL(oversized_suffix);

    memset(maximum_prefix, '0', maximum_prefix_chars);
    maximum_prefix[maximum_prefix_chars] = '\0';
    memset(maximum_suffix, '0', maximum_suffix_chars);
    maximum_suffix[maximum_suffix_chars] = '\0';
    memset(oversized_prefix, '0', maximum_prefix_chars + 2);
    oversized_prefix[maximum_prefix_chars + 2] = '\0';
    memset(oversized_suffix, '0', maximum_suffix_chars + 2);
    oversized_suffix[maximum_suffix_chars + 2] = '\0';

    TEST_ASSERT_TRUE(parse_notify(
        &message, maximum_prefix, "0200", 0, VALID_MERKLE_BRANCH, "647025b5"));
    TEST_ASSERT_EQUAL_UINT16(MAX_COINBASE_PREFIX_LEN, s_test_job.coinbase_prefix_len);

    TEST_ASSERT_TRUE(parse_notify(
        &message, "0100", maximum_suffix, 0, VALID_MERKLE_BRANCH, "647025b5"));
    TEST_ASSERT_EQUAL_UINT16(MAX_COINBASE_SUFFIX_LEN, s_test_job.coinbase_suffix_len);

    TEST_ASSERT_FALSE(parse_notify(
        &message, oversized_prefix, "0200", 0, VALID_MERKLE_BRANCH, "647025b5"));
    TEST_ASSERT_FALSE(parse_notify(
        &message, "0100", oversized_suffix, 0, VALID_MERKLE_BRANCH, "647025b5"));

    free(maximum_prefix);
    free(maximum_suffix);
    free(oversized_prefix);
    free(oversized_suffix);
}

TEST_CASE("Parse mining.notify into caller-owned job storage", "[mining.notify]")
{
    StratumApiV1Message message = {};
    miner_job_t job = {};
    char *json = create_notify_message(
        "0100", "0200", 0, VALID_MERKLE_BRANCH, "647025b5");
    TEST_ASSERT_NOT_NULL(json);

    TEST_ASSERT_FALSE(parse_with_job(&message, json, NULL));
    TEST_ASSERT_FALSE(parse_with_job(&message, json, &job));

    job.coinbase_prefix = malloc(MAX_COINBASE_PREFIX_LEN);
    TEST_ASSERT_NOT_NULL(job.coinbase_prefix);
    TEST_ASSERT_FALSE(parse_with_job(&message, json, &job));

    job.coinbase_suffix = malloc(MAX_COINBASE_SUFFIX_LEN);
    TEST_ASSERT_NOT_NULL(job.coinbase_suffix);
    TEST_ASSERT_TRUE(parse_with_job(&message, json, &job));
    TEST_ASSERT_EQUAL_PTR(&job, message.job);
    TEST_ASSERT_EQUAL_STRING("job-1", job.job_id);
    TEST_ASSERT_EQUAL_UINT16(2, job.coinbase_prefix_len);
    TEST_ASSERT_EQUAL_UINT16(2, job.coinbase_suffix_len);

    miner_job_t parsed_job = job;
    TEST_ASSERT_FALSE(parse_with_job(
        &message,
        "{\"method\":\"mining.notify\",\"params\":[\"job\",\"invalid\",\"0100\",\"0200\",[],\"20000000\",\"1705ae3a\",\"647025b5\",true]}",
        &job));
    TEST_ASSERT_EQUAL_MEMORY(&parsed_job, &job, sizeof(job));

    STRATUM_V1_reset_message(&message);
    TEST_ASSERT_NULL(message.job);
    TEST_ASSERT_EQUAL_STRING("job-1", job.job_id);

    free(job.coinbase_prefix);
    free(job.coinbase_suffix);
    free(json);
}

TEST_CASE("Parse stratum mining.notify validates Merkle branch limits", "[mining.notify]")
{
    StratumApiV1Message message = {};

    TEST_ASSERT_FALSE(parse_notify(
        &message, "0100", "0200", MAX_MERKLE_BRANCHES + 1,
        VALID_MERKLE_BRANCH, "647025b5"));
    TEST_ASSERT_FALSE(parse_notify(
        &message, "0100", "0200", 1, "00", "647025b5"));
}

TEST_CASE("Parse stratum mining.notify checks future time with a synced clock", "[mining.notify]")
{
    StratumApiV1Message message = {};
    time_t now = time(NULL);
    bool accepted = parse_notify(
        &message, "0100", "0200", 0, VALID_MERKLE_BRANCH, "ffffffff");

    TEST_ASSERT_EQUAL(now <= 1704067200, accepted);
}

TEST_CASE("Parse stratum result falls back for unstructured errors", "[stratum]")
{
    StratumApiV1Message message = {};

    TEST_ASSERT_TRUE(STRATUM_V1_parse(
        &message, "{\"id\":1,\"result\":null,\"error\":7,\"reject-reason\":\"stale\"}"));
    TEST_ASSERT_FALSE(message.response_success);
    TEST_ASSERT_EQUAL_STRING("stale", message.error_str);

    TEST_ASSERT_TRUE(STRATUM_V1_parse(&message, "{\"id\":2,\"error\":7}"));
    TEST_ASSERT_FALSE(message.response_success);
    TEST_ASSERT_EQUAL_STRING("unknown", message.error_str);
    STRATUM_V1_reset_message(&message);
}

TEST_CASE("Parse stratum rejects missing and non-array method parameters", "[stratum]")
{
    const char *invalid_messages[] = {
        "{\"method\":\"mining.set_difficulty\"}",
        "{\"method\":\"mining.set_difficulty\",\"params\":{}}",
        "{\"method\":\"mining.set_version_mask\"}",
        "{\"method\":\"mining.set_version_mask\",\"params\":{}}",
        "{\"method\":\"mining.set_extranonce\"}",
        "{\"method\":\"mining.set_extranonce\",\"params\":{}}",
        "{\"method\":\"client.show_message\"}",
        "{\"method\":\"client.show_message\",\"params\":{}}",
    };

    for (size_t index = 0; index < sizeof(invalid_messages) / sizeof(invalid_messages[0]); ++index) {
        StratumApiV1Message message = {};
        TEST_ASSERT_FALSE(STRATUM_V1_parse(&message, invalid_messages[index]));
    }
}

TEST_CASE("Parse stratum rejects remaining invalid value types", "[stratum]")
{
    const char *invalid_messages[] = {
        "{\"id\":\"one\",\"method\":7}",
        "{\"method\":\"mining.set_extranonce\",\"params\":[\"deadbeef\",\"eight\"]}",
        "{\"method\":\"mining.set_extranonce\",\"params\":[\"00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff00\",8]}",
        "{\"result\":[[],\"deadbeef\",\"eight\"],\"error\":null}",
        "{\"result\":{\"version-rolling\":true},\"error\":null}",
        "{\"result\":{\"version-rolling\":true,\"version-rolling.mask\":7},\"error\":null}",
    };

    for (size_t index = 0; index < sizeof(invalid_messages) / sizeof(invalid_messages[0]); ++index) {
        StratumApiV1Message message = {};
        TEST_ASSERT_FALSE(STRATUM_V1_parse(&message, invalid_messages[index]));
        STRATUM_V1_reset_message(&message);
    }
}

TEST_CASE("Parse stratum mining.notify rejects remaining invalid field types", "[mining.notify]")
{
    StratumApiV1Message message = {};
    const char *invalid_messages[] = {
        "{\"method\":\"mining.notify\",\"params\":[\"1\",\"0000000000000000000000000000000000000000000000000000000000000000\",\"0100\",7,[],\"20000000\",\"1705ae3a\",\"647025b5\",true]}",
        "{\"method\":\"mining.notify\",\"params\":[\"1\",\"0000000000000000000000000000000000000000000000000000000000000000\",\"0100\",\"0200\",[],\"20000000\",7,\"647025b5\",true]}",
        "{\"method\":\"mining.notify\",\"params\":[\"1\",\"0000000000000000000000000000000000000000000000000000000000000000\",\"0100\",\"0200\",[],\"20000000\",\"1705ae3a\",7,true]}",
    };

    for (size_t index = 0; index < sizeof(invalid_messages) / sizeof(invalid_messages[0]); ++index) {
        TEST_ASSERT_FALSE(STRATUM_V1_parse(&message, invalid_messages[index]));
    }
}

TEST_CASE("Parse stratum handles malformed structured errors", "[stratum]")
{
    const char *messages[] = {
        "{\"result\":null,\"error\":[21]}",
        "{\"result\":null,\"error\":[21,7]}",
        "{\"result\":null,\"error\":{}}",
        "{\"result\":null,\"error\":{\"message\":7}}",
        "{\"result\":false,\"error\":null}",
        "{\"result\":false,\"error\":null,\"reject-reason\":7}",
    };

    for (size_t index = 0; index < sizeof(messages) / sizeof(messages[0]); ++index) {
        StratumApiV1Message message = {};
        TEST_ASSERT_TRUE(STRATUM_V1_parse(&message, messages[index]));
        TEST_ASSERT_FALSE(message.response_success);
        TEST_ASSERT_EQUAL_STRING("unknown", message.error_str);
        STRATUM_V1_reset_message(&message);
    }
}

TEST_CASE("Parse stratum rejects unsupported result containers", "[stratum]")
{
    StratumApiV1Message message = {};

    TEST_ASSERT_FALSE(STRATUM_V1_parse(&message, "{\"result\":[]}"));
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&message, "{\"result\":{}}"));
}

TEST_CASE("Parse mining.notify into exact ASIC-neutral job fields", "[mining.notify]")
{
    static const char sequential_hash[] =
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
    static const char merkle_branches[] =
        "[\"000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\"]";
    static const uint8_t expected_previous_hash[32] = {
        0x03, 0x02, 0x01, 0x00, 0x07, 0x06, 0x05, 0x04,
        0x0b, 0x0a, 0x09, 0x08, 0x0f, 0x0e, 0x0d, 0x0c,
        0x13, 0x12, 0x11, 0x10, 0x17, 0x16, 0x15, 0x14,
        0x1b, 0x1a, 0x19, 0x18, 0x1f, 0x1e, 0x1d, 0x1c,
    };
    static const uint8_t expected_merkle_hash[32] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    };
    static const uint8_t expected_prefix[] = {0x00, 0x11, 0x22, 0xaa, 0xff};
    static const uint8_t expected_suffix[] = {0xde, 0xad, 0xbe, 0xef};
    StratumApiV1Message message = {};

    TEST_ASSERT_TRUE(parse_notify_fields(
        &message, "job-42", sequential_hash, "001122AAff", "deadBEEF",
        merkle_branches, "A0000001", "1705Ae3A", "647025B5", "true"));

    const miner_job_t *job = &s_test_job;
    TEST_ASSERT_EQUAL_INT(MINING_NOTIFY, message.method);
    TEST_ASSERT_EQUAL_PTR(job, message.job);
    TEST_ASSERT_EQUAL_INT(JOB_TYPE_V1, job->type);
    TEST_ASSERT_EQUAL_STRING("job-42", job->job_id);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_previous_hash, job->prev_hash, sizeof(job->prev_hash));
    TEST_ASSERT_EQUAL_UINT16(sizeof(expected_prefix), job->coinbase_prefix_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_prefix, job->coinbase_prefix, sizeof(expected_prefix));
    TEST_ASSERT_EQUAL_UINT16(sizeof(expected_suffix), job->coinbase_suffix_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_suffix, job->coinbase_suffix, sizeof(expected_suffix));
    TEST_ASSERT_EQUAL_UINT8(1, job->merkle_path_count);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_merkle_hash, job->merkle_path[0], sizeof(job->merkle_path[0]));
    TEST_ASSERT_EQUAL_HEX32(0xa0000001, job->version);
    TEST_ASSERT_EQUAL_HEX32(0x1705ae3a, job->nbits);
    TEST_ASSERT_EQUAL_HEX32(0x647025b5, job->ntime);
    TEST_ASSERT_TRUE(job->clean_jobs);
}

TEST_CASE("Reject non-hexadecimal mining.notify fields", "[mining.notify]")
{
    static const char valid_hash[] =
        "0000000000000000000000000000000000000000000000000000000000000000";
    char invalid_hash[65];
    memset(invalid_hash, '0', sizeof(invalid_hash) - 1);
    invalid_hash[sizeof(invalid_hash) - 2] = 'g';
    invalid_hash[sizeof(invalid_hash) - 1] = '\0';
    char invalid_branches[70];
    (void)snprintf(invalid_branches, sizeof(invalid_branches), "[\"%s\"]", invalid_hash);
    StratumApiV1Message message = {};

    TEST_ASSERT_FALSE(parse_notify_fields(
        &message, "job", invalid_hash, "00", "00", "[]",
        "20000000", "1705ae3a", "647025b5", "true"));
    TEST_ASSERT_FALSE(parse_notify_fields(
        &message, "job", valid_hash, "0g", "00", "[]",
        "20000000", "1705ae3a", "647025b5", "true"));
    TEST_ASSERT_FALSE(parse_notify_fields(
        &message, "job", valid_hash, "00", "0g", "[]",
        "20000000", "1705ae3a", "647025b5", "true"));
    TEST_ASSERT_FALSE(parse_notify_fields(
        &message, "job", valid_hash, "00", "00", invalid_branches,
        "20000000", "1705ae3a", "647025b5", "true"));
    TEST_ASSERT_FALSE(parse_notify_fields(
        &message, "job", valid_hash, "00", "00", "[]",
        "2000000g", "1705ae3a", "647025b5", "true"));
    TEST_ASSERT_FALSE(parse_notify_fields(
        &message, "job", valid_hash, "00", "00", "[]",
        "20000000", "1705aez3", "647025b5", "true"));
    TEST_ASSERT_FALSE(parse_notify_fields(
        &message, "job", valid_hash, "00", "00", "[]",
        "20000000", "1705ae3a", "647025bg", "true"));
}

TEST_CASE("Enforce mining.notify job ID and clean-jobs contracts", "[mining.notify]")
{
    char maximum_job_id[MAX_JOB_ID_LEN];
    memset(maximum_job_id, 'j', sizeof(maximum_job_id) - 1);
    maximum_job_id[sizeof(maximum_job_id) - 1] = '\0';
    char oversized_job_id[MAX_JOB_ID_LEN + 1];
    memset(oversized_job_id, 'j', sizeof(oversized_job_id) - 1);
    oversized_job_id[sizeof(oversized_job_id) - 1] = '\0';
    StratumApiV1Message message = {};

    TEST_ASSERT_TRUE(parse_notify_fields(
        &message, maximum_job_id, VALID_PREVIOUS_BLOCK_HASH, "00", "00", "[]",
        "20000000", "1705ae3a", "647025b5", "false"));
    TEST_ASSERT_EQUAL_STRING(maximum_job_id, s_test_job.job_id);
    TEST_ASSERT_FALSE(s_test_job.clean_jobs);

    TEST_ASSERT_FALSE(parse_notify_fields(
        &message, oversized_job_id, VALID_PREVIOUS_BLOCK_HASH, "00", "00", "[]",
        "20000000", "1705ae3a", "647025b5", "true"));
    TEST_ASSERT_FALSE(parse_notify_fields(
        &message, "job", VALID_PREVIOUS_BLOCK_HASH, "00", "00", "[]",
        "20000000", "1705ae3a", "647025b5", "0"));
    TEST_ASSERT_FALSE(STRATUM_V1_parse(
        &message,
        "{\"method\":\"mining.notify\",\"params\":[\"job\",\"0000000000000000000000000000000000000000000000000000000000000000\",\"00\",\"00\",[],\"20000000\",\"1705ae3a\",\"647025b5\"]}"));
}

TEST_CASE("Validate SV1 hexadecimal configuration values", "[stratum]")
{
    static const char *invalid_set_masks[] = {
        "{\"method\":\"mining.set_version_mask\",\"params\":[\"\"]}",
        "{\"method\":\"mining.set_version_mask\",\"params\":[\"xyz\"]}",
        "{\"method\":\"mining.set_version_mask\",\"params\":[\"123456789\"]}",
    };
    static const char *invalid_configure_masks[] = {
        "{\"result\":{\"version-rolling\":true,\"version-rolling.mask\":\"\"}}",
        "{\"result\":{\"version-rolling\":true,\"version-rolling.mask\":\"xyz\"}}",
        "{\"result\":{\"version-rolling\":true,\"version-rolling.mask\":\"123456789\"}}",
    };
    StratumApiV1Message message = {};

    for (size_t index = 0; index < sizeof(invalid_set_masks) / sizeof(invalid_set_masks[0]); ++index) {
        TEST_ASSERT_FALSE(STRATUM_V1_parse(&message, invalid_set_masks[index]));
        TEST_ASSERT_FALSE(STRATUM_V1_parse(&message, invalid_configure_masks[index]));
    }

    TEST_ASSERT_TRUE(STRATUM_V1_parse(
        &message, "{\"method\":\"mining.set_version_mask\",\"params\":[\"1FFFE000\"]}"));
    TEST_ASSERT_EQUAL_HEX32(BIP320_VERSION_ROLLING_MASK, message.version_mask);
}

TEST_CASE("Validate SV1 extranonce hexadecimal strings and integer sizes", "[stratum]")
{
    static const char *invalid_messages[] = {
        "{\"method\":\"mining.set_extranonce\",\"params\":[\"deadbeeg\",8]}",
        "{\"method\":\"mining.set_extranonce\",\"params\":[\"deadbeef\",4.5]}",
        "{\"method\":\"mining.set_extranonce\",\"params\":[\"deadbeef\",1e20]}",
        "{\"result\":[[],\"deadbeeg\",8],\"error\":null}",
        "{\"result\":[[],\"deadbeef\",4.5],\"error\":null}",
        "{\"result\":[[],\"deadbeef\",1e20],\"error\":null}",
    };

    for (size_t index = 0; index < sizeof(invalid_messages) / sizeof(invalid_messages[0]); ++index) {
        StratumApiV1Message message = {};
        TEST_ASSERT_FALSE(STRATUM_V1_parse(&message, invalid_messages[index]));
        STRATUM_V1_reset_message(&message);
    }

    StratumApiV1Message message = {};
    TEST_ASSERT_TRUE(STRATUM_V1_parse(
        &message, "{\"method\":\"mining.set_extranonce\",\"params\":[\"DEADBEEF\",8]}"));
    TEST_ASSERT_EQUAL_STRING("DEADBEEF", message.extranonce_str);
    STRATUM_V1_reset_message(&message);
}

TEST_CASE("Accept only bounded SV1 difficulty values", "[mining.set_difficulty]")
{
    StratumApiV1Message message = {};

    TEST_ASSERT_TRUE(STRATUM_V1_parse(
        &message, "{\"method\":\"mining.set_difficulty\",\"params\":[0.0001]}"));
    TEST_ASSERT_EQUAL_DOUBLE(0.0001, message.new_difficulty);
    TEST_ASSERT_TRUE(STRATUM_V1_parse(
        &message, "{\"method\":\"mining.set_difficulty\",\"params\":[4294967295]}"));
    TEST_ASSERT_EQUAL_DOUBLE(4294967295.0, message.new_difficulty);
    TEST_ASSERT_FALSE(STRATUM_V1_parse(
        &message, "{\"method\":\"mining.set_difficulty\",\"params\":[0.0000999]}"));
    TEST_ASSERT_FALSE(STRATUM_V1_parse(
        &message, "{\"method\":\"mining.set_difficulty\",\"params\":[4294967296]}"));
}

TEST_CASE("Validate the complete SV1 JSON envelope", "[stratum]")
{
    StratumApiV1Message message = {};

    TEST_ASSERT_FALSE(STRATUM_V1_parse(NULL, "{\"result\":true}"));
    message.method = MINING_NOTIFY;
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&message, NULL));
    TEST_ASSERT_EQUAL_INT(METHOD_UNKNOWN, message.method);
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&message, "[]"));
    TEST_ASSERT_FALSE(STRATUM_V1_parse(&message, "{\"result\":true} trailing"));
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&message, "{\"id\":null,\"result\":true}\n\t"));
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&message, "{\"id\":2147483647,\"result\":true}"));
    TEST_ASSERT_EQUAL_INT(INT_MAX, message.message_id);
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&message, "{\"id\":-2147483648,\"result\":true}"));
    TEST_ASSERT_EQUAL_INT(INT_MIN, message.message_id);

    static const char *invalid_ids[] = {
        "{\"id\":\"one\",\"result\":true}",
        "{\"id\":1.5,\"result\":true}",
        "{\"id\":2147483648,\"result\":true}",
        "{\"id\":-2147483649,\"result\":true}",
    };
    for (size_t index = 0; index < sizeof(invalid_ids) / sizeof(invalid_ids[0]); ++index) {
        TEST_ASSERT_FALSE(STRATUM_V1_parse(&message, invalid_ids[index]));
    }
    TEST_ASSERT_FALSE(STRATUM_V1_parse(
        &message, "{\"id\":1,\"method\":null,\"result\":true}"));
}

TEST_CASE("Require parameter arrays for payload-free SV1 requests", "[stratum]")
{
    static const char *invalid_messages[] = {
        "{\"method\":\"mining.ping\"}",
        "{\"method\":\"mining.ping\",\"params\":{}}",
        "{\"method\":\"client.reconnect\"}",
        "{\"method\":\"client.reconnect\",\"params\":{}}",
        "{\"method\":\"client.get_version\"}",
        "{\"method\":\"client.get_version\",\"params\":{}}",
    };
    StratumApiV1Message message = {};

    for (size_t index = 0; index < sizeof(invalid_messages) / sizeof(invalid_messages[0]); ++index) {
        TEST_ASSERT_FALSE(STRATUM_V1_parse(&message, invalid_messages[index]));
    }
}

TEST_CASE("Reset every owned SV1 message field", "[stratum]")
{
    miner_job_t caller_job = {.version = UINT32_MAX};
    StratumApiV1Message message = {
        .extranonce_str = strdup("0011"),
        .extranonce_2_len = 8,
        .message_id = 42,
        .method = MINING_NOTIFY,
        .new_difficulty = 100.0,
        .version_mask = BIP320_VERSION_ROLLING_MASK,
        .response_success = true,
        .error_str = strdup("error"),
        .show_message = strdup("message"),
        .version_string = strdup("version"),
        .job = &caller_job,
    };

    TEST_ASSERT_NOT_NULL(message.extranonce_str);
    TEST_ASSERT_NOT_NULL(message.error_str);
    TEST_ASSERT_NOT_NULL(message.show_message);
    TEST_ASSERT_NOT_NULL(message.version_string);
    STRATUM_V1_reset_message(&message);

    TEST_ASSERT_NULL(message.extranonce_str);
    TEST_ASSERT_EQUAL_INT(0, message.extranonce_2_len);
    TEST_ASSERT_EQUAL_INT(-1, message.message_id);
    TEST_ASSERT_EQUAL_INT(METHOD_UNKNOWN, message.method);
    TEST_ASSERT_NULL(message.job);
    TEST_ASSERT_EQUAL_HEX32(UINT32_MAX, caller_job.version);
    TEST_ASSERT_EQUAL_DOUBLE(0.0, message.new_difficulty);
    TEST_ASSERT_EQUAL_HEX32(0, message.version_mask);
    TEST_ASSERT_FALSE(message.response_success);
    TEST_ASSERT_NULL(message.error_str);
    TEST_ASSERT_NULL(message.show_message);
    TEST_ASSERT_NULL(message.version_string);

    STRATUM_V1_reset_message(&message);
    STRATUM_V1_reset_message(NULL);
}

TEST_CASE("Bound pool-provided SV1 text fields", "[stratum]")
{
    char long_text[301];
    memset(long_text, 'x', sizeof(long_text) - 1);
    long_text[sizeof(long_text) - 1] = '\0';
    char json[400];
    StratumApiV1Message message = {};

    (void)snprintf(json, sizeof(json),
                   "{\"method\":\"client.show_message\",\"params\":[\"%s\"]}", long_text);
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&message, json));
    TEST_ASSERT_EQUAL_UINT(MAX_POOL_MESSAGE_LEN, strlen(message.show_message));

    (void)snprintf(json, sizeof(json),
                   "{\"result\":null,\"error\":[21,\"%s\",null]}", long_text);
    TEST_ASSERT_TRUE(STRATUM_V1_parse(&message, json));
    TEST_ASSERT_EQUAL_UINT(256, strlen(message.error_str));
    STRATUM_V1_reset_message(&message);
}
