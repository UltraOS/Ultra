#pragma once

#include <common/types.h>
#include <common/helpers.h>
#include <common/align.h>

struct rb_node {
    ptr_t parent_meta;
    struct rb_node *left, *right;
};
#define rb_entry(ptr, type, member) container_of(ptr, type, member)

#define rb_node_parent(node) \
    ((struct rb_node*)ALIGN_DOWN((node)->parent_meta, alignof(struct rb_node)))

// Root of a generic red-black tree
struct rb_root {
    struct rb_node *root;
};
#define RB_ROOT_INIT { .root = nullptr }

/*
 * Root of a generic red-black tree, but with the first (left-most)
 * element cached. Use the rb_*_cached helpers to make sure the cache
 * is maintained properly. Use rb_first_cached to retrieve the first
 * element
 */
struct rb_root_cached {
    struct rb_root base;
    struct rb_node *left_most;
};
#define RB_ROOT_CACHED_INIT { .base = RB_ROOT_INIT, .left_most = nullptr }

/*
 * Link 'node' against 'parent', 'parent_link' is one of
 * '&parent->left' or '&parent->right'
 *
 * NOTE: this is a low level helper, as such you must manually ensure that
 *       the tree is balanced & this node is colored afterwards. Usually
 *       this is accomplished by calling rb_node_balance() on the node.
 */
static inline void rb_node_link(
    struct rb_node *node, struct rb_node *parent,
    struct rb_node **parent_link
)
{
    node->left = nullptr;
    node->right = nullptr;
    node->parent_meta = (ptr_t)parent;

    *parent_link = node;
}

// Balance the tree at 'root' after linking 'node' against its parent
void rb_node_balance(struct rb_node *node, struct rb_root *root);

/*
 * Balance the tree at 'root' after linking 'node' against its parent.
 * 'is_leftmost' is true if 'node' is the leftmost node in the tree
 */
static inline void rb_node_balance_cached(
    struct rb_node *node, struct rb_root_cached *root, bool is_leftmost
)
{
    if (is_leftmost)
        root->left_most = node;
    rb_node_balance(node, &root->base);
}

/*
 * Insert 'node' into the tree specified by 'root'.
 * 'parent' is the node in this tree where 'node' should be inserted.
 * 'parent_link' is one of '&parent->left' or '&parent->right'
 */
static inline void rb_node_insert_at(
    struct rb_node *node, struct rb_node *parent,
    struct rb_node **parent_link, struct rb_root *root
)
{
    rb_node_link(node, parent, parent_link);
    rb_node_balance(node, root);
}

/*
 * Insert 'node' into the tree specified by 'root'.
 * 'parent' is the node in this tree where 'node' should be inserted.
 * 'parent_link' is one of '&parent->left' or '&parent->right'
 * 'is_leftmost' is true if 'node' is the leftmost node in the tree
 */
static inline void rb_node_insert_at_cached(
    struct rb_node *node, struct rb_node *parent,
    struct rb_node **parent_link, struct rb_root_cached *root,
    bool is_leftmost
)
{
    rb_node_link(node, parent, parent_link);
    rb_node_balance_cached(node, root, is_leftmost);
}

/*
 * Return values:
 * true -> new_node < old_node
 * false -> new_node >= old_node
 */
typedef bool (*rb_node_cmp_less_cb_t)(
    const struct rb_node *new_node, const struct rb_node *old_node
);

/*
 * Return values:
 * ret <  0 -> new_node < old_node
 * ret == 0 -> new_node == old_node
 * ret >  0 -> new_node > old_node
 */
typedef int (*rb_node_cmp_threeway_cb_t)(
    const struct rb_node *new_node, const struct rb_node *old_node
);

static inline void rb_node_insert(
    struct rb_node *node, struct rb_root *root,
    rb_node_cmp_less_cb_t cmp_cb
)
{
    struct rb_node *parent = nullptr;
    struct rb_node **parent_link = &root->root;

    while (*parent_link) {
        parent = *parent_link;

        if (cmp_cb(node, parent))
            parent_link = &parent->left;
        else
            parent_link = &parent->right;
    }

    rb_node_insert_at(node, parent, parent_link, root);
}

static inline struct rb_node *rb_node_find_or_insert(
    struct rb_node *node, struct rb_root *root,
    rb_node_cmp_threeway_cb_t cmp_cb
)
{
    struct rb_node *parent = nullptr;
    struct rb_node **parent_link = &root->root;
    int res;

    while (*parent_link) {
        parent = *parent_link;
        res = cmp_cb(node, parent);

        if (res < 0)
            parent_link = &parent->left;
        else if (res > 0)
            parent_link = &parent->right;
        else
            return parent;
    }

    rb_node_insert_at(node, parent, parent_link, root);
    return nullptr;
}

static inline void rb_node_insert_cached(
    struct rb_node *node, struct rb_root_cached *root,
    rb_node_cmp_less_cb_t cmp_cb
)
{
    struct rb_node *parent = nullptr;
    struct rb_node **parent_link = &root->base.root;
    bool is_leftmost = true;

    while (*parent_link) {
        parent = *parent_link;

        if (cmp_cb(node, parent)) {
            parent_link = &parent->left;
        } else {
            parent_link = &parent->right;
            is_leftmost = false;
        }
    }

    rb_node_insert_at_cached(node, parent, parent_link, root, is_leftmost);
}

static inline struct rb_node *rb_node_find_or_insert_cached(
    struct rb_node *node, struct rb_root_cached *root,
    rb_node_cmp_threeway_cb_t cmp_cb
)
{
    struct rb_node *parent = nullptr;
    struct rb_node **parent_link = &root->base.root;
    int res;
    bool is_leftmost = true;

    while (*parent_link) {
        parent = *parent_link;
        res = cmp_cb(node, parent);

        if (res < 0) {
            parent_link = &parent->left;
        } else if (res > 0) {
            parent_link = &parent->right;
            is_leftmost = false;
        } else {
            return parent;
        }
    }

    rb_node_insert_at_cached(node, parent, parent_link, root, is_leftmost);
    return nullptr;
}

/*
 * Return values:
 * ret <  0 -> key < node
 * ret == 0 -> key == node
 * ret >  0 -> key > node
 */
typedef int (*rb_node_key_cmp_cb_t)(
    const void *key, const struct rb_node *node
);

static inline struct rb_node *rb_node_find(
    const void *key, struct rb_root *root,
    rb_node_key_cmp_cb_t cmp_cb
)
{
    struct rb_node *node = root->root;
    int res;

    while (node) {
        res = cmp_cb(key, node);

        if (res < 0)
            node = node->left;
        else if (res > 0)
            node = node->right;
        else
            return node;
    }

    return nullptr;
}

#define rb_first_cached(root) ((root)->left_most)

static inline struct rb_node *rb_first(struct rb_root *root)
{
    struct rb_node *node;

    node = root->root;
    if (node == nullptr)
        return nullptr;

    while (node->left)
        node = node->left;

    return node;
}

static inline struct rb_node *rb_last(struct rb_root *root)
{
    struct rb_node *node;

    node = root->root;
    if (node == nullptr)
        return nullptr;

    while (node->right)
        node = node->right;

    return node;
}

struct rb_node *rb_next(struct rb_node *node);
struct rb_node *rb_prev(struct rb_node *node);

void rb_node_replace(
    struct rb_node *old, struct rb_node *new, struct rb_root *root
);

static inline void rb_node_replace_cached(
    struct rb_node *old, struct rb_node *new, struct rb_root_cached *root
)
{
    if (old == root->left_most)
        root->left_most = new;

    rb_node_replace(old, new, &root->base);
}

void rb_node_remove(struct rb_node *node, struct rb_root *root);

static inline void rb_node_remove_cached(
    struct rb_node *node, struct rb_root_cached *root
)
{
    if (node == root->left_most)
        root->left_most = rb_next(node);

    rb_node_remove(node, &root->base);
}
