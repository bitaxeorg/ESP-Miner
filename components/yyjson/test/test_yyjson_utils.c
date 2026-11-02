#include <string.h>
#include <stdlib.h>
#include "unity.h"
#include "yyjson.h"
#include "yyjson_utils.h"

static yyjson_mut_doc *json_to_mut_doc(const char *json_str)
{
    yyjson_doc *doc = yyjson_read(json_str, strlen(json_str), 0);
    if (!doc) {
        return NULL;
    }
    yyjson_mut_doc *mut_doc = yyjson_doc_mut_copy(doc, NULL);
    yyjson_doc_free(doc);
    return mut_doc;
}

static void assert_json_val_equals(yyjson_mut_val *actual, const char *expected_json)
{
    TEST_ASSERT_NOT_NULL(actual);
    yyjson_doc *exp_doc = yyjson_read(expected_json, strlen(expected_json), 0);
    TEST_ASSERT_NOT_NULL(exp_doc);
    yyjson_mut_doc *exp_mut = yyjson_doc_mut_copy(exp_doc, NULL);
    yyjson_doc_free(exp_doc);
    TEST_ASSERT_NOT_NULL(exp_mut);

    bool equals = yyjson_mut_equals(actual, yyjson_mut_doc_get_root(exp_mut));
    if (!equals) {
        char *act_str = yyjson_mut_val_write(actual, 0, NULL);
        printf("FAIL: Expected: %s, Got: %s\n", expected_json, act_str ? act_str : "NULL");
        free(act_str);
    }
    TEST_ASSERT_TRUE(equals);
    yyjson_mut_doc_free(exp_mut);
}

TEST_CASE("yyjson_diff_objects handles null or non-object inputs", "[yyjson]")
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    TEST_ASSERT_NOT_NULL(doc);

    yyjson_mut_val *valid_obj = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_int(doc, valid_obj, "a", 1);

    yyjson_mut_val *non_obj = yyjson_mut_int(doc, 42);

    // new_obj == NULL
    TEST_ASSERT_NULL(yyjson_diff_objects(doc, NULL, NULL));
    TEST_ASSERT_NULL(yyjson_diff_objects(doc, valid_obj, NULL));

    // new_obj is not an object
    TEST_ASSERT_NULL(yyjson_diff_objects(doc, NULL, non_obj));
    TEST_ASSERT_NULL(yyjson_diff_objects(doc, valid_obj, non_obj));

    // old_obj is not an object -> treated like NULL, returns full copy of new_obj
    yyjson_mut_val *diff = yyjson_diff_objects(doc, non_obj, valid_obj);
    assert_json_val_equals(diff, "{\"a\":1}");

    yyjson_mut_doc_free(doc);
}

TEST_CASE("yyjson_diff_objects produces full copy when old_obj is NULL", "[yyjson]")
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc *new_doc = json_to_mut_doc("{\"power\":15.2,\"status\":\"mining\",\"fan\":{\"rpm\":4500}}");
    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_NOT_NULL(new_doc);

    yyjson_mut_val *diff = yyjson_diff_objects(doc, NULL, yyjson_mut_doc_get_root(new_doc));
    assert_json_val_equals(diff, "{\"power\":15.2,\"status\":\"mining\",\"fan\":{\"rpm\":4500}}");

    yyjson_mut_doc_free(new_doc);
    yyjson_mut_doc_free(doc);
}

TEST_CASE("yyjson_diff_objects returns NULL when objects are identical", "[yyjson]")
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    const char *json_str = "{\"num\":100,\"text\":\"abc\",\"flag\":true,\"arr\":[1,2,3],\"nested\":{\"x\":1}}";
    yyjson_mut_doc *old_doc = json_to_mut_doc(json_str);
    yyjson_mut_doc *new_doc = json_to_mut_doc(json_str);

    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_NOT_NULL(old_doc);
    TEST_ASSERT_NOT_NULL(new_doc);

    yyjson_mut_val *diff = yyjson_diff_objects(doc, yyjson_mut_doc_get_root(old_doc), yyjson_mut_doc_get_root(new_doc));
    TEST_ASSERT_NULL(diff);

    yyjson_mut_doc_free(old_doc);
    yyjson_mut_doc_free(new_doc);
    yyjson_mut_doc_free(doc);
}

TEST_CASE("yyjson_diff_objects detects scalar and null modifications", "[yyjson]")
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc *old_doc = json_to_mut_doc("{\"a\":1,\"b\":\"orig\",\"c\":true,\"d\":null,\"unchanged\":123}");
    yyjson_mut_doc *new_doc = json_to_mut_doc("{\"a\":2,\"b\":\"new\",\"c\":false,\"d\":\"now_set\",\"unchanged\":123}");

    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_NOT_NULL(old_doc);
    TEST_ASSERT_NOT_NULL(new_doc);

    yyjson_mut_val *diff = yyjson_diff_objects(doc, yyjson_mut_doc_get_root(old_doc), yyjson_mut_doc_get_root(new_doc));
    assert_json_val_equals(diff, "{\"a\":2,\"b\":\"new\",\"c\":false,\"d\":\"now_set\"}");

    yyjson_mut_doc_free(old_doc);
    yyjson_mut_doc_free(new_doc);
    yyjson_mut_doc_free(doc);
}

TEST_CASE("yyjson_diff_objects detects added fields", "[yyjson]")
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc *old_doc = json_to_mut_doc("{\"existing\":10}");
    yyjson_mut_doc *new_doc = json_to_mut_doc("{\"existing\":10,\"added\":\"val\"}");

    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_NOT_NULL(old_doc);
    TEST_ASSERT_NOT_NULL(new_doc);

    yyjson_mut_val *diff = yyjson_diff_objects(doc, yyjson_mut_doc_get_root(old_doc), yyjson_mut_doc_get_root(new_doc));
    assert_json_val_equals(diff, "{\"added\":\"val\"}");

    yyjson_mut_doc_free(old_doc);
    yyjson_mut_doc_free(new_doc);
    yyjson_mut_doc_free(doc);
}

TEST_CASE("yyjson_diff_objects recursively diffs nested objects", "[yyjson]")
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc *old_doc = json_to_mut_doc("{\"sub\":{\"x\":1,\"y\":2},\"flat\":\"same\"}");
    yyjson_mut_doc *new_doc = json_to_mut_doc("{\"sub\":{\"x\":1,\"y\":99},\"flat\":\"same\"}");

    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_NOT_NULL(old_doc);
    TEST_ASSERT_NOT_NULL(new_doc);

    yyjson_mut_val *diff = yyjson_diff_objects(doc, yyjson_mut_doc_get_root(old_doc), yyjson_mut_doc_get_root(new_doc));
    assert_json_val_equals(diff, "{\"sub\":{\"y\":99}}");

    yyjson_mut_doc_free(old_doc);
    yyjson_mut_doc_free(new_doc);
    yyjson_mut_doc_free(doc);
}

TEST_CASE("yyjson_diff_objects detects array modifications", "[yyjson]")
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc *old_doc = json_to_mut_doc("{\"arr\":[1,2],\"keep\":1}");
    yyjson_mut_doc *new_doc = json_to_mut_doc("{\"arr\":[1,3],\"keep\":1}");

    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_NOT_NULL(old_doc);
    TEST_ASSERT_NOT_NULL(new_doc);

    yyjson_mut_val *diff = yyjson_diff_objects(doc, yyjson_mut_doc_get_root(old_doc), yyjson_mut_doc_get_root(new_doc));
    assert_json_val_equals(diff, "{\"arr\":[1,3]}");

    yyjson_mut_doc_free(old_doc);
    yyjson_mut_doc_free(new_doc);
    yyjson_mut_doc_free(doc);
}

TEST_CASE("yyjson_diff_objects works with psram allocator", "[yyjson]")
{
    const yyjson_alc *alc = yyjson_psram_alc();
    yyjson_mut_doc *doc = yyjson_mut_doc_new(alc);
    yyjson_mut_doc *old_doc = json_to_mut_doc("{\"val\":1}");
    yyjson_mut_doc *new_doc = json_to_mut_doc("{\"val\":2}");

    TEST_ASSERT_NOT_NULL(doc);
    TEST_ASSERT_NOT_NULL(old_doc);
    TEST_ASSERT_NOT_NULL(new_doc);

    yyjson_mut_val *diff = yyjson_diff_objects(doc, yyjson_mut_doc_get_root(old_doc), yyjson_mut_doc_get_root(new_doc));
    assert_json_val_equals(diff, "{\"val\":2}");

    yyjson_mut_doc_free(old_doc);
    yyjson_mut_doc_free(new_doc);
    yyjson_mut_doc_free(doc);
}
