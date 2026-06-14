#pragma once

#include <common/types.h>
#include <common/helpers.h>

// A circular doubly linked list
struct list_link {
    struct list_link *prev;
    struct list_link *next;
};

#define LIST_INIT(name) { &(name), &(name) }

#define LIST_HEAD(name) \
    struct list_link name = LIST_INIT(name)

static inline void list_init(struct list_link *link)
{
    link->next = link;
    link->prev = link;
}

static inline void list_insert(
    struct list_link *new_link, struct list_link *prev,
    struct list_link *next
)
{
    next->prev = new_link;
    new_link->next = next;
    new_link->prev = prev;
    prev->next = new_link;
}

static inline void list_insert_next(
    struct list_link *head, struct list_link *new_link
)
{
    list_insert(new_link, head, head->next);
}

static inline void list_insert_prev(
    struct list_link *head, struct list_link *new_link
)
{
    list_insert(new_link, head->prev, head);
}

static inline void list_remove(struct list_link *link)
{
    link->prev->next = link->next;
    link->next->prev = link->prev;
    list_init(link);
}

static inline bool list_is_empty(const struct list_link *head)
{
    return head->next == head;
}

#define list_entry(ptr, type, member) container_of(ptr, type, member)

#define list_first_entry(head, type, member) \
    list_entry((head)->next, type, member)

#define list_last_entry(head, type, member) \
    list_entry((head)->prev, type, member)

#define list_for_each(cursor, head) \
    for (cursor = (head)->next; cursor != (head); cursor = cursor->next)

#define list_for_each_safe(cursor, tmp, head)                         \
    for (cursor = (head)->next, tmp = cursor->next; cursor != (head); \
         cursor = tmp, tmp = cursor->next)

#define list_for_each_entry(cursor, head, member)                    \
    for (cursor = list_entry((head)->next, typeof(*cursor), member); \
         &cursor->member != (head);                                  \
         cursor = list_entry(cursor->member.next, typeof(*cursor), member))

#define list_for_each_entry_safe(cursor, tmp, head, member)              \
    for (cursor = list_entry((head)->next, typeof(*cursor), member),     \
         tmp = list_entry(cursor->member.next, typeof(*cursor), member); \
         &cursor->member != (head);                                      \
         cursor = tmp, tmp = list_entry(tmp->member.next, typeof(*tmp), member))
