#pragma once

#include <common/types.h>
#include <common/helpers.h>

// A singly (non-atomic) linked list
struct slist_node {
    struct slist_node *next;
};

struct slist_head {
    struct slist_node *first;
};

#define SLIST_INIT(name) { nullptr }

#define SLIST_HEAD(name) \
    struct slist_head name = SLIST_INIT(name)

static inline void slist_init(struct slist_head *head)
{
    head->first = nullptr;
}

static inline bool slist_is_empty(const struct slist_head *head)
{
    return head->first == nullptr;
}

static inline void slist_push(struct slist_head *head, struct slist_node *node)
{
    node->next = head->first;
    head->first = node;
}

static inline struct slist_node *slist_pop(struct slist_head *head)
{
    struct slist_node *node = head->first;

    if (node)
        head->first = node->next;
    return node;
}

#define slist_entry(ptr, type, member) container_of(ptr, type, member)
