#include "unity.h"

#include <string.h>

#include "gridpool_direct_builder.h"
#include "utils.h"

static void assert_script_hex(const char *address, const char *expected_hex)
{
    uint8_t script[GRIDPOOL_DIRECT_MAX_SCRIPT_PUBKEY_LEN];
    size_t script_len = 0;
    char script_hex[GRIDPOOL_DIRECT_MAX_SCRIPT_PUBKEY_LEN * 2 + 1];

    TEST_ASSERT_EQUAL(ESP_OK, gridpool_direct_address_to_script(address, script, sizeof(script), &script_len));
    TEST_ASSERT_EQUAL(strlen(expected_hex) / 2, script_len);
    TEST_ASSERT_EQUAL(strlen(expected_hex), bin2hex(script, script_len, script_hex, sizeof(script_hex)));
    TEST_ASSERT_EQUAL_STRING(expected_hex, script_hex);
}

TEST_CASE("GridPool direct encodes standard address script types", "[gridpool_direct]")
{
    assert_script_hex("1DYwPTnC4NgEmoqbLbcRqoSzVeH3ehmGbV",
                      "76a91489abcdef0123456789abcdef0123456789abcdef88ac");
    assert_script_hex("33MGnVL6rnKqt6Jjt3HbRqWJrhwy65dMhS",
                      "a914123456789abcdef0123456789abcdef01234567887");
    assert_script_hex("bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x",
                      "0014aabbccddeeff00112233445566778899aabbccdd");
    assert_script_hex("bc1pllhdmn9m42vcsamx24zrxgs3qrl7ahwvhw4fnzrhve25gvezzyqqc0cgpt",
                      "5120ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100");
}

TEST_CASE("GridPool direct parses payout outputs", "[gridpool_direct]")
{
    const char *json =
        "{"
        "\"sequence\":42,"
        "\"coinbaseOutputs\":["
        "{\"value\":1000,\"address\":\"bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x\",\"username\":\"ignored\"},"
        "{\"value\":2000,\"address\":\"bc1pllhdmn9m42vcsamx24zrxgs3qrl7ahwvhw4fnzrhve25gvezzyqqc0cgpt\"}"
        "],"
        "\"network\":{}"
        "}";

    gridpool_direct_payout_output_t outputs[4];
    size_t output_count = 0;
    uint64_t sequence = 0;

    TEST_ASSERT_EQUAL(ESP_OK, gridpool_direct_parse_payout_outputs_json(json, outputs, 4, &output_count, &sequence));
    TEST_ASSERT_EQUAL_UINT64(42, sequence);
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)output_count);
    TEST_ASSERT_EQUAL_UINT64(1000, outputs[0].value_sats);
    TEST_ASSERT_EQUAL_STRING("bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x", outputs[0].address);
    TEST_ASSERT_EQUAL_UINT64(2000, outputs[1].value_sats);
}

TEST_CASE("GridPool direct builds coinbase, merkle path, header, and bm_job", "[gridpool_direct]")
{
    const char *template_json =
        "{"
        "\"version\":536870912,"
        "\"previousblockhash\":\"000000000000000000005db6b8c2a0a97709a987278e991a8053118694a47fe6\","
        "\"bits\":\"17020f79\","
        "\"curtime\":1779245082,"
        "\"height\":950178,"
        "\"coinbasevalue\":1000000,"
        "\"default_witness_commitment\":\"6a24aa21a9ed0000000000000000000000000000000000000000000000000000000000000000\","
        "\"transactions\":["
        "{\"data\":\"00\",\"txid\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"weight\":4},"
        "{\"data\":\"00\",\"txid\":\"ffffffffffffffffffffffffffffffff00000000000000000000000000000000\",\"weight\":4},"
        "{\"data\":\"00\",\"txid\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaabbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\",\"weight\":4}"
        "]"
        "}";

    gridpool_direct_template_t template_data = { 0 };
    TEST_ASSERT_EQUAL(ESP_OK, gridpool_direct_parse_template_json(template_json, &template_data));
    TEST_ASSERT_EQUAL_UINT32(0x20000000, template_data.version);
    TEST_ASSERT_EQUAL_UINT32(0x17020f79, template_data.nbits);
    TEST_ASSERT_EQUAL_UINT32(1779245082, template_data.curtime);
    TEST_ASSERT_EQUAL_UINT32(950178, template_data.height);
    TEST_ASSERT_EQUAL_UINT64(1000000, template_data.coinbase_value);
    TEST_ASSERT_EQUAL_UINT32(3, (uint32_t)template_data.tx_count);
    TEST_ASSERT_EQUAL_HEX8(0xef, template_data.txid_hashes[0]);

    gridpool_direct_payout_output_t outputs[] = {
        {
            .value_sats = 1000,
            .address = "bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x",
        },
        {
            .value_sats = 2000,
            .address = "bc1pllhdmn9m42vcsamx24zrxgs3qrl7ahwvhw4fnzrhve25gvezzyqqc0cgpt",
        },
    };
    uint8_t extranonce[] = { 0x01, 0x02, 0x03, 0x04 };
    gridpool_direct_job_t job = { 0 };

    TEST_ASSERT_EQUAL(ESP_OK, gridpool_direct_build_job(&template_data,
                                                        "bc1q42aueh0wluqpzg3ng32kvaugnx4thnxa7y625x",
                                                        outputs,
                                                        2,
                                                        extranonce,
                                                        sizeof(extranonce),
                                                        "/GridPoolTest/",
                                                        1234.0,
                                                        &job));

    TEST_ASSERT_EQUAL_UINT64(997000, job.slot0_value_sats);
    TEST_ASSERT_NOT_NULL(job.coinbase_tx);
    TEST_ASSERT_TRUE(job.coinbase_tx_len > 0);
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)job.merkle_path_count);
    TEST_ASSERT_EQUAL_UINT32(template_data.version, job.bm_job.version);
    TEST_ASSERT_EQUAL_UINT32(template_data.nbits, job.bm_job.target);
    TEST_ASSERT_EQUAL_UINT32(template_data.curtime, job.bm_job.ntime);
    TEST_ASSERT_EQUAL_INT(1234, (int)job.bm_job.pool_diff);

    uint8_t recomputed_root[32];
    TEST_ASSERT_EQUAL(ESP_OK, gridpool_direct_compute_root_from_path(job.coinbase_txid,
                                                                     job.merkle_path,
                                                                     job.merkle_path_count,
                                                                     recomputed_root));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(job.merkle_root, recomputed_root, 32);

    uint8_t header[80];
    TEST_ASSERT_EQUAL(ESP_OK, gridpool_direct_serialize_header(&job, 0x11223344, job.version, header));
    TEST_ASSERT_EQUAL_HEX8(0x00, header[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, header[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, header[2]);
    TEST_ASSERT_EQUAL_HEX8(0x20, header[3]);
    TEST_ASSERT_EQUAL_HEX8(0xe6, header[4]);
    TEST_ASSERT_EQUAL_HEX8(0x7f, header[5]);
    TEST_ASSERT_EQUAL_HEX8(0xa4, header[6]);
    TEST_ASSERT_EQUAL_HEX8(0x94, header[7]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(job.merkle_root, header + 36, 32);
    TEST_ASSERT_EQUAL_HEX8(0x5a, header[68]);
    TEST_ASSERT_EQUAL_HEX8(0x1d, header[69]);
    TEST_ASSERT_EQUAL_HEX8(0x32, header[70]);
    TEST_ASSERT_EQUAL_HEX8(0x6a, header[71]);
    TEST_ASSERT_EQUAL_HEX8(0x79, header[72]);
    TEST_ASSERT_EQUAL_HEX8(0x0f, header[73]);
    TEST_ASSERT_EQUAL_HEX8(0x02, header[74]);
    TEST_ASSERT_EQUAL_HEX8(0x17, header[75]);
    TEST_ASSERT_EQUAL_HEX8(0x44, header[76]);
    TEST_ASSERT_EQUAL_HEX8(0x33, header[77]);
    TEST_ASSERT_EQUAL_HEX8(0x22, header[78]);
    TEST_ASSERT_EQUAL_HEX8(0x11, header[79]);

    char coinbase_hex[1024];
    TEST_ASSERT_TRUE(job.coinbase_tx_len * 2 + 1 <= sizeof(coinbase_hex));
    bin2hex(job.coinbase_tx, job.coinbase_tx_len, coinbase_hex, sizeof(coinbase_hex));
    const char *slot0_script = "0014aabbccddeeff00112233445566778899aabbccdd";
    const char *taproot_script = "5120ffeeddccbbaa99887766554433221100ffeeddccbbaa99887766554433221100";
    TEST_ASSERT_NOT_NULL(strstr(coinbase_hex, slot0_script));
    TEST_ASSERT_NOT_NULL(strstr(coinbase_hex, taproot_script));
    TEST_ASSERT_TRUE(strstr(coinbase_hex, slot0_script) < strstr(coinbase_hex, taproot_script));

    gridpool_direct_job_free(&job);
    gridpool_direct_template_free(&template_data);
}
