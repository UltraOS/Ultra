#include <common/list.h>

#include <test_harness.h>

struct list_item {
    int value;
    struct list_link link;
};

TEST_CASE(list_basics)
{
    struct list_link link = LIST_INIT(link);
    ASSERT_EQ(link.prev, &link);
    ASSERT_EQ(link.next, &link);
}

TEST_CASE(list_init_runtime)
{
    struct list_link link;

    link.next = nullptr;
    link.prev = nullptr;

    list_init(&link);
    ASSERT_EQ(link.next, &link);
    ASSERT_EQ(link.prev, &link);
    ASSERT(list_is_empty(&link));
}

TEST_CASE(list_head_macro)
{
    LIST_HEAD(head);

    ASSERT_EQ(head.next, &head);
    ASSERT_EQ(head.prev, &head);
    ASSERT(list_is_empty(&head));
}

TEST_CASE(list_insert_next_order)
{
    LIST_HEAD(head);
    struct list_link a, b, c;

    // Inserting at the front repeatedly reverses insertion order.
    list_insert_next(&head, &a);
    list_insert_next(&head, &b);
    list_insert_next(&head, &c);

    ASSERT_FALSE(list_is_empty(&head));

    ASSERT_EQ(head.next, &c);
    ASSERT_EQ(c.next, &b);
    ASSERT_EQ(b.next, &a);
    ASSERT_EQ(a.next, &head);

    // Verify the back links match.
    ASSERT_EQ(head.prev, &a);
    ASSERT_EQ(a.prev, &b);
    ASSERT_EQ(b.prev, &c);
    ASSERT_EQ(c.prev, &head);
}

TEST_CASE(list_insert_prev_order)
{
    LIST_HEAD(head);
    struct list_link a, b, c;

    // Inserting at the back preserves insertion order.
    list_insert_prev(&head, &a);
    list_insert_prev(&head, &b);
    list_insert_prev(&head, &c);

    ASSERT_EQ(head.next, &a);
    ASSERT_EQ(a.next, &b);
    ASSERT_EQ(b.next, &c);
    ASSERT_EQ(c.next, &head);

    ASSERT_EQ(head.prev, &c);
    ASSERT_EQ(c.prev, &b);
    ASSERT_EQ(b.prev, &a);
    ASSERT_EQ(a.prev, &head);
}

TEST_CASE(list_remove_middle)
{
    LIST_HEAD(head);
    struct list_link a, b, c;

    list_insert_prev(&head, &a);
    list_insert_prev(&head, &b);
    list_insert_prev(&head, &c);

    list_remove(&b);

    // b should be re-initialized to point at itself.
    ASSERT_EQ(b.next, &b);
    ASSERT_EQ(b.prev, &b);
    ASSERT(list_is_empty(&b));

    // The list should now be a <-> c.
    ASSERT_EQ(head.next, &a);
    ASSERT_EQ(a.next, &c);
    ASSERT_EQ(c.next, &head);
    ASSERT_EQ(c.prev, &a);
    ASSERT_EQ(a.prev, &head);
}

TEST_CASE(list_remove_until_empty)
{
    LIST_HEAD(head);
    struct list_link a, b;

    list_insert_prev(&head, &a);
    list_insert_prev(&head, &b);
    ASSERT_FALSE(list_is_empty(&head));

    list_remove(&a);
    ASSERT_FALSE(list_is_empty(&head));

    list_remove(&b);
    ASSERT(list_is_empty(&head));
    ASSERT_EQ(head.next, &head);
    ASSERT_EQ(head.prev, &head);
}

TEST_CASE(list_first_last_entry)
{
    LIST_HEAD(head);
    struct list_item a = { .value = 10 };
    struct list_item b = { .value = 20 };
    struct list_item c = { .value = 30 };

    list_insert_prev(&head, &a.link);
    list_insert_prev(&head, &b.link);
    list_insert_prev(&head, &c.link);

    ASSERT_EQ(list_first_entry(&head, struct list_item, link)->value, 10);
    ASSERT_EQ(list_last_entry(&head, struct list_item, link)->value, 30);

    ASSERT_EQ(list_entry(&b.link, struct list_item, link), &b);
}

TEST_CASE(list_for_each_iteration)
{
    LIST_HEAD(head);
    struct list_link a, b, c;
    struct list_link *cursor;
    struct list_link *expected[3];
    size_t i;

    list_insert_prev(&head, &a);
    list_insert_prev(&head, &b);
    list_insert_prev(&head, &c);

    expected[0] = &a;
    expected[1] = &b;
    expected[2] = &c;

    i = 0;
    list_for_each(cursor, &head) {
        ASSERT_EQ(cursor, expected[i]);
        i++;
    }
    ASSERT_EQ(i, 3);
}

TEST_CASE(list_for_each_empty)
{
    LIST_HEAD(head);
    struct list_link *cursor;
    size_t count = 0;

    list_for_each(cursor, &head)
        count++;

    ASSERT_EQ(count, 0);
}

TEST_CASE(list_for_each_safe_remove_all)
{
    LIST_HEAD(head);
    struct list_link a, b, c;
    struct list_link *cursor, *tmp;
    size_t count = 0;

    list_insert_prev(&head, &a);
    list_insert_prev(&head, &b);
    list_insert_prev(&head, &c);

    list_for_each_safe(cursor, tmp, &head) {
        list_remove(cursor);
        count++;
    }

    ASSERT_EQ(count, 3);
    ASSERT(list_is_empty(&head));
    ASSERT(list_is_empty(&a));
    ASSERT(list_is_empty(&b));
    ASSERT(list_is_empty(&c));
}

TEST_CASE(list_for_each_entry_iteration)
{
    LIST_HEAD(head);
    struct list_item a = { .value = 1 };
    struct list_item b = { .value = 2 };
    struct list_item c = { .value = 3 };
    struct list_item *cursor;
    int sum = 0;
    size_t count = 0;

    list_insert_prev(&head, &a.link);
    list_insert_prev(&head, &b.link);
    list_insert_prev(&head, &c.link);

    list_for_each_entry(cursor, &head, link) {
        sum += cursor->value;
        count++;
    }

    ASSERT_EQ(count, 3);
    ASSERT_EQ(sum, 6);
}

TEST_CASE(list_for_each_entry_safe_remove_even)
{
    LIST_HEAD(head);
    struct list_item a = { .value = 1 };
    struct list_item b = { .value = 2 };
    struct list_item c = { .value = 3 };
    struct list_item d = { .value = 4 };
    struct list_item *cursor, *tmp;
    int sum = 0;
    size_t count = 0;

    list_insert_prev(&head, &a.link);
    list_insert_prev(&head, &b.link);
    list_insert_prev(&head, &c.link);
    list_insert_prev(&head, &d.link);

    // Remove the even-valued entries while iterating.
    list_for_each_entry_safe(cursor, tmp, &head, link) {
        if (cursor->value % 2 == 0)
            list_remove(&cursor->link);
    }

    list_for_each_entry(cursor, &head, link) {
        sum += cursor->value;
        count++;
    }

    ASSERT_EQ(count, 2);
    ASSERT_EQ(sum, 4);

    ASSERT_EQ(list_first_entry(&head, struct list_item, link)->value, 1);
    ASSERT_EQ(list_last_entry(&head, struct list_item, link)->value, 3);

    ASSERT(list_is_empty(&b.link));
    ASSERT(list_is_empty(&d.link));
}

TEST_CASE(list_for_each_entry_safe_remove_all)
{
    LIST_HEAD(head);
    struct list_item a = { .value = 1 };
    struct list_item b = { .value = 2 };
    struct list_item c = { .value = 3 };
    struct list_item *cursor, *tmp;
    size_t count = 0;

    list_insert_prev(&head, &a.link);
    list_insert_prev(&head, &b.link);
    list_insert_prev(&head, &c.link);

    list_for_each_entry_safe(cursor, tmp, &head, link) {
        list_remove(&cursor->link);
        count++;
    }

    ASSERT_EQ(count, 3);
    ASSERT(list_is_empty(&head));
}
