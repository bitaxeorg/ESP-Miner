#include <string.h>
#include "unity.h"
#include "http_auth.h"

// Fixed salt 0x00..0x0f used for deterministic hashing vectors.
static const uint8_t TEST_SALT[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

// Precomputed with Python: sha256(bytes(range(16)) + b"hunter2").
#define STORED_HUNTER2 "000102030405060708090a0b0c0d0e0f:" \
                       "e8d1e18507861757b13674f2e9d0e9149ed6667f432ad5b8220f9c049a29dc54"

// sha256(bytes(range(16)) + b"p@ss:word:with:colons")
#define STORED_COLONS  "000102030405060708090a0b0c0d0e0f:" \
                       "449fe551ace188a331dfa9f04d67d281b729a8ef84c818b48b2de40ca82a7e9a"

TEST_CASE("http_auth: constant-time equals", "[http_auth]")
{
    TEST_ASSERT_TRUE(http_auth_ct_equals("abc", "abc"));
    TEST_ASSERT_TRUE(http_auth_ct_equals("", ""));
    TEST_ASSERT_FALSE(http_auth_ct_equals("abc", "abd"));
    TEST_ASSERT_FALSE(http_auth_ct_equals("abc", "ab"));   // different length
    TEST_ASSERT_FALSE(http_auth_ct_equals("ab", "abc"));   // different length
    TEST_ASSERT_FALSE(http_auth_ct_equals(NULL, "abc"));
    TEST_ASSERT_FALSE(http_auth_ct_equals("abc", NULL));
}

TEST_CASE("http_auth: compute_hash produces expected salt:hash", "[http_auth]")
{
    char out[128];
    TEST_ASSERT_EQUAL(ESP_OK, http_auth_compute_hash(TEST_SALT, sizeof(TEST_SALT), "hunter2", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING(STORED_HUNTER2, out);
}

TEST_CASE("http_auth: compute_hash rejects tiny output buffer", "[http_auth]")
{
    char out[8];
    TEST_ASSERT_NOT_EQUAL(ESP_OK, http_auth_compute_hash(TEST_SALT, sizeof(TEST_SALT), "hunter2", out, sizeof(out)));
}

TEST_CASE("http_auth: verify_hash accepts the right password", "[http_auth]")
{
    TEST_ASSERT_TRUE(http_auth_verify_hash(STORED_HUNTER2, "hunter2"));
}

TEST_CASE("http_auth: verify_hash rejects the wrong password", "[http_auth]")
{
    TEST_ASSERT_FALSE(http_auth_verify_hash(STORED_HUNTER2, "hunter3"));
    TEST_ASSERT_FALSE(http_auth_verify_hash(STORED_HUNTER2, ""));
    TEST_ASSERT_FALSE(http_auth_verify_hash(STORED_HUNTER2, "Hunter2"));
}

TEST_CASE("http_auth: verify_hash rejects malformed stored strings", "[http_auth]")
{
    TEST_ASSERT_FALSE(http_auth_verify_hash("", "hunter2"));
    TEST_ASSERT_FALSE(http_auth_verify_hash("no-colon-here", "hunter2"));
    TEST_ASSERT_FALSE(http_auth_verify_hash(":onlyhash", "hunter2"));     // empty salt
    TEST_ASSERT_FALSE(http_auth_verify_hash("zzzz:abcd", "hunter2"));     // non-hex salt
    TEST_ASSERT_FALSE(http_auth_verify_hash(NULL, "hunter2"));
    TEST_ASSERT_FALSE(http_auth_verify_hash(STORED_HUNTER2, NULL));
}

TEST_CASE("http_auth: roundtrip hash then verify", "[http_auth]")
{
    // A different salt would be used in production; here we reuse TEST_SALT to
    // prove compute + verify agree.
    char out[128];
    TEST_ASSERT_EQUAL(ESP_OK, http_auth_compute_hash(TEST_SALT, sizeof(TEST_SALT), "s3cr3t!", out, sizeof(out)));
    TEST_ASSERT_TRUE(http_auth_verify_hash(out, "s3cr3t!"));
    TEST_ASSERT_FALSE(http_auth_verify_hash(out, "s3cr3t"));
}

TEST_CASE("http_auth: parse_basic decodes user and pass", "[http_auth]")
{
    char user[33];
    char pass[65];
    // base64("admin:hunter2")
    TEST_ASSERT_EQUAL(ESP_OK, http_auth_parse_basic("Basic YWRtaW46aHVudGVyMg==",
                                                    user, sizeof(user), pass, sizeof(pass)));
    TEST_ASSERT_EQUAL_STRING("admin", user);
    TEST_ASSERT_EQUAL_STRING("hunter2", pass);
}

TEST_CASE("http_auth: parse_basic keeps colons in the password", "[http_auth]")
{
    char user[33];
    char pass[65];
    // base64("admin:p@ss:word:with:colons")
    TEST_ASSERT_EQUAL(ESP_OK, http_auth_parse_basic("Basic YWRtaW46cEBzczp3b3JkOndpdGg6Y29sb25z",
                                                    user, sizeof(user), pass, sizeof(pass)));
    TEST_ASSERT_EQUAL_STRING("admin", user);
    TEST_ASSERT_EQUAL_STRING("p@ss:word:with:colons", pass);
}

TEST_CASE("http_auth: parse_basic is case-insensitive on the scheme", "[http_auth]")
{
    char user[33];
    char pass[65];
    TEST_ASSERT_EQUAL(ESP_OK, http_auth_parse_basic("basic YWRtaW46aHVudGVyMg==",
                                                    user, sizeof(user), pass, sizeof(pass)));
    TEST_ASSERT_EQUAL_STRING("admin", user);
}

TEST_CASE("http_auth: parse_basic rejects malformed headers", "[http_auth]")
{
    char user[33];
    char pass[65];
    // Wrong scheme.
    TEST_ASSERT_NOT_EQUAL(ESP_OK, http_auth_parse_basic("Bearer YWRtaW46aHVudGVyMg==",
                                                        user, sizeof(user), pass, sizeof(pass)));
    // No colon inside decoded token: base64("user").
    TEST_ASSERT_NOT_EQUAL(ESP_OK, http_auth_parse_basic("Basic dXNlcg==",
                                                        user, sizeof(user), pass, sizeof(pass)));
    // Empty token.
    TEST_ASSERT_NOT_EQUAL(ESP_OK, http_auth_parse_basic("Basic ",
                                                        user, sizeof(user), pass, sizeof(pass)));
    // Not base64.
    TEST_ASSERT_NOT_EQUAL(ESP_OK, http_auth_parse_basic("Basic !!!not-base64!!!",
                                                        user, sizeof(user), pass, sizeof(pass)));
}

TEST_CASE("http_auth: parse_basic enforces output bounds", "[http_auth]")
{
    char user[3];   // too small for "admin"
    char pass[65];
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE,
                      http_auth_parse_basic("Basic YWRtaW46aHVudGVyMg==",
                                            user, sizeof(user), pass, sizeof(pass)));
}

TEST_CASE("http_auth: full parse + verify flow", "[http_auth]")
{
    char user[33];
    char pass[65];
    // A client presenting admin / p@ss:word:with:colons must verify against the
    // stored hash for that password.
    TEST_ASSERT_EQUAL(ESP_OK, http_auth_parse_basic("Basic YWRtaW46cEBzczp3b3JkOndpdGg6Y29sb25z",
                                                    user, sizeof(user), pass, sizeof(pass)));
    TEST_ASSERT_TRUE(http_auth_ct_equals("admin", user));
    TEST_ASSERT_TRUE(http_auth_verify_hash(STORED_COLONS, pass));
    TEST_ASSERT_FALSE(http_auth_verify_hash(STORED_HUNTER2, pass));
}
