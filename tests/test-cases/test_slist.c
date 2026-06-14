#include <common/slist.h>

#include <test_harness.h>

struct slist_item {
    int value;
    struct slist_node node;
};

TEST_CASE(slist_basics)
{
    struct slist_head head = SLIST_INIT(head);
    ASSERT(head.first == nullptr);
    ASSERT(slist_is_empty(&head));
}

TEST_CASE(slist_init_runtime)
{
    struct slist_head head;

    head.first = (struct slist_node*)0x1234;

    slist_init(&head);
    ASSERT(head.first == nullptr);
    ASSERT(slist_is_empty(&head));
}

TEST_CASE(slist_head_macro)
{
    SLIST_HEAD(head);

    ASSERT(head.first == nullptr);
    ASSERT(slist_is_empty(&head));
}

TEST_CASE(slist_push_order)
{
    SLIST_HEAD(head);
    struct slist_node a, b, c;

    // Push is LIFO, the most recently pushed node ends up first.
    slist_push(&head, &a);
    ASSERT_FALSE(slist_is_empty(&head));
    ASSERT_EQ(head.first, &a);
    ASSERT(a.next == nullptr);

    slist_push(&head, &b);
    ASSERT_EQ(head.first, &b);
    ASSERT_EQ(b.next, &a);

    slist_push(&head, &c);
    ASSERT_EQ(head.first, &c);
    ASSERT_EQ(c.next, &b);
    ASSERT_EQ(b.next, &a);
    ASSERT(a.next == nullptr);
}

TEST_CASE(slist_pop_order)
{
    SLIST_HEAD(head);
    struct slist_node a, b, c;

    slist_push(&head, &a);
    slist_push(&head, &b);
    slist_push(&head, &c);

    // Pop returns nodes in reverse push order.
    ASSERT_EQ(slist_pop(&head), &c);
    ASSERT_EQ(slist_pop(&head), &b);
    ASSERT_EQ(slist_pop(&head), &a);

    ASSERT(slist_is_empty(&head));
}

TEST_CASE(slist_pop_empty)
{
    SLIST_HEAD(head);

    ASSERT(slist_pop(&head) == nullptr);
    ASSERT(slist_is_empty(&head));
}

TEST_CASE(slist_push_pop_interleaved)
{
    SLIST_HEAD(head);
    struct slist_node a, b, c;

    slist_push(&head, &a);
    slist_push(&head, &b);

    ASSERT_EQ(slist_pop(&head), &b);

    slist_push(&head, &c);

    ASSERT_EQ(slist_pop(&head), &c);
    ASSERT_EQ(slist_pop(&head), &a);
    ASSERT(slist_pop(&head) == nullptr);
    ASSERT(slist_is_empty(&head));
}

TEST_CASE(slist_entry_and_traversal)
{
    SLIST_HEAD(head);
    struct slist_item a = { .value = 10 };
    struct slist_item b = { .value = 20 };
    struct slist_item c = { .value = 30 };
    struct slist_node *cursor;
    int expected[3] = { 30, 20, 10 };
    size_t i;

    slist_push(&head, &a.node);
    slist_push(&head, &b.node);
    slist_push(&head, &c.node);

    ASSERT_EQ(slist_entry(&b.node, struct slist_item, node), &b);

    i = 0;
    for (cursor = head.first; cursor != nullptr; cursor = cursor->next) {
        struct slist_item *item = slist_entry(cursor, struct slist_item, node);
        ASSERT_EQ(item->value, expected[i]);
        i++;
    }
    ASSERT_EQ(i, 3);
}
