#include <stdlib.h>

#include "unity.h"
#include "work_queue.h"

static int freed_v1;
static int freed_standard;
static int freed_extended;

static void count_and_free(void *data, int *counter)
{
    TEST_ASSERT_NOT_NULL(data);
    (*counter)++;
    free(data);
}

static void free_v1(void *data)
{
    count_and_free(data, &freed_v1);
}

static void free_standard(void *data)
{
    count_and_free(data, &freed_standard);
}

static void free_extended(void *data)
{
    count_and_free(data, &freed_extended);
}

static work_queue_item_t make_item(work_queue *queue, work_item_kind_t kind,
                                   void (*free_fn)(void *))
{
    work_item_kind_t *stored_kind = malloc(sizeof(*stored_kind));
    TEST_ASSERT_NOT_NULL(stored_kind);
    *stored_kind = kind;
    return work_queue_item_create(queue, stored_kind, kind, free_fn);
}

TEST_CASE("work queue binds ownership to source kind and epoch", "[work_queue]")
{
    work_queue queue;
    queue_init(&queue);
    freed_v1 = 0;
    freed_standard = 0;
    freed_extended = 0;

    queue_set_source(&queue, WORK_ITEM_STRATUM_V1);
    work_queue_source_t v1_source = queue_get_source(&queue);

    TEST_ASSERT_TRUE(queue_enqueue(
        &queue, make_item(&queue, WORK_ITEM_STRATUM_V1, free_v1)));
    TEST_ASSERT_FALSE(queue_enqueue(
        &queue, make_item(&queue, WORK_ITEM_STRATUM_V2_STANDARD, free_standard)));

    work_queue_item_t item = queue_dequeue(&queue);
    TEST_ASSERT_EQUAL(WORK_ITEM_STRATUM_V1, item.kind);
    TEST_ASSERT_EQUAL(WORK_ITEM_STRATUM_V1,
                      *(work_item_kind_t *)item.data);
    TEST_ASSERT_EQUAL_UINT32(v1_source.epoch, item.source_epoch);
    work_queue_item_free(&item);

    TEST_ASSERT_NULL(item.data);
    TEST_ASSERT_EQUAL(WORK_ITEM_NONE, item.kind);
    TEST_ASSERT_NULL(item.free_fn);

    work_queue_item_t stale = make_item(&queue, WORK_ITEM_STRATUM_V1, free_v1);
    queue_set_source(&queue, WORK_ITEM_STRATUM_V1);
    work_queue_source_t next_v1_source = queue_get_source(&queue);
    TEST_ASSERT_NOT_EQUAL(v1_source.epoch, next_v1_source.epoch);
    TEST_ASSERT_FALSE(queue_enqueue(&queue, stale));

    queue_set_source(&queue, WORK_ITEM_STRATUM_V2_EXTENDED);
    TEST_ASSERT_TRUE(queue_enqueue(
        &queue, make_item(&queue, WORK_ITEM_STRATUM_V2_EXTENDED, free_extended)));
    queue_clear(&queue);
    TEST_ASSERT_EQUAL_INT(2, freed_v1);
    TEST_ASSERT_EQUAL_INT(1, freed_standard);
    TEST_ASSERT_EQUAL_INT(1, freed_extended);
}

TEST_CASE("work queue replaces oldest item and timeout is empty", "[work_queue]")
{
    work_queue queue;
    queue_init(&queue);
    freed_v1 = 0;
    queue_set_source(&queue, WORK_ITEM_STRATUM_V1);

    for (int i = 0; i < QUEUE_SIZE + 1; i++) {
        TEST_ASSERT_TRUE(queue_enqueue(
            &queue, make_item(&queue, WORK_ITEM_STRATUM_V1, free_v1)));
    }
    TEST_ASSERT_EQUAL_INT(1, freed_v1);

    queue_clear(&queue);
    TEST_ASSERT_EQUAL_INT(QUEUE_SIZE + 1, freed_v1);

    work_queue_item_t item = queue_dequeue_timeout(&queue, 0);
    TEST_ASSERT_NULL(item.data);
    TEST_ASSERT_EQUAL(WORK_ITEM_NONE, item.kind);
    TEST_ASSERT_NULL(item.free_fn);
}
