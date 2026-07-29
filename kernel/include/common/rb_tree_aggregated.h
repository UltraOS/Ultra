#pragma once

#include <private/rb_tree.h>

#include <common/attributes.h>
#include <common/minmax.h>

struct rb_tree_aggregated_ops {
    /*
     * Recompute the aggregated value starting at the 'from' node all the way
     * up to the 'until' node (exclusive).
     *
     * Note that if it is detected that the aggregated value is the same as the
     * previous value, the function should return early as an optimization.
     */
    void (*propagate)(struct rb_node *from, struct rb_node *until);

    /*
     * The 'old' node has been rotated and is now a child of the 'new' node.
     *
     * The 'new' node is expected to take over the aggregated value of the
     * 'old' node, while the 'old' node is expected to recalculate it manually.
     */
    void (*rotate)(struct rb_node *old, struct rb_node *new);

    /*
     * Copy the aggregated value from the 'from' node to the 'to' node.
     */
    void (*copy)(struct rb_node *from, struct rb_node *to);
};

/*
 * Standalone type for the rotate callback above. The color-fixup primitives
 * take it directly (instead of the whole ops struct) since a rotation is the
 * only structural change they perform.
 */
typedef void (*rb_aggregated_rotate_cb_t)(
    struct rb_node *old, struct rb_node *new
);

/*
 * Color-fixup primitives shared with the non-aggregated tree. They live in the
 * translation unit so the heavy rebalancing logic is emitted only once: the
 * aggregated wrappers below hand them a real rotate callback, while the plain
 * variants in rb_tree.c hand them a no-op that the compiler folds away.
 */
void rb_node_balance_after_insert(
    struct rb_node *node, struct rb_root *root,
    rb_aggregated_rotate_cb_t rotate
);

void rb_node_balance_after_remove(
    struct rb_node *parent, struct rb_root *root,
    rb_aggregated_rotate_cb_t rotate
);

/*
 * Unlink 'node' from the tree and return the node at which the color fixup
 * must resume, or nullptr if the removal preserved every invariant on its own.
 *
 * This is the augmented analogue of the binary-search-tree splice: the
 * aggregated value is repaired through the 'ops' callbacks in lockstep with
 * the structural changes. It is kept fully inline so that callers supplying a
 * compile-time-constant 'ops' (including the no-op set) pay no indirection.
 */
static ALWAYS_INLINE struct rb_node *rb_node_remove_and_compute_rebalance(
    struct rb_node *node, struct rb_root *root,
    const struct rb_tree_aggregated_ops *ops
)
{
    struct rb_node *child = node->right;
    struct rb_node *temp = node->left;
    struct rb_node *parent, *rebalance;
    ptr_t parent_meta;

    if (temp == nullptr) {
        /*
         * At most one child. If it exists it must be red while 'node' is
         * black, so recoloring it locally sidesteps the color fixup.
         */
        parent_meta = node->parent_meta;
        parent = rb_node_parent(node);
        rb_change_child(node, child, parent, root);

        if (child) {
            child->parent_meta = parent_meta;
            rebalance = nullptr;
        } else {
            rebalance =
                (parent_meta & PARENT_META_COLOR) == RB_COLOR_BLACK
                ? parent : nullptr;
        }

        temp = parent;
    } else if (child == nullptr) {
        // A lone left child, which must be red, so recolor it in place.
        parent_meta = node->parent_meta;
        temp->parent_meta = parent_meta;
        parent = rb_node_parent(node);
        rb_change_child(node, temp, parent, root);
        rebalance = nullptr;
        temp = parent;
    } else {
        struct rb_node *successor, *successor_child;

        successor = child;
        temp = child->left;

        if (temp == nullptr) {
            // The successor is the immediate right child of 'node'.
            parent = successor;
            successor_child = successor->right;
            ops->copy(node, successor);
        } else {
            /*
             * The successor is the left-most node beneath 'node's right
             * child, splice it out of its current spot first.
             */
            do {
                parent = successor;
                successor = temp;
                temp = temp->left;
            } while (temp);

            successor_child = successor->right;
            parent->left = successor_child;
            successor->right = child;
            rb_set_parent(child, successor);

            ops->copy(node, successor);
            ops->propagate(parent, successor);
        }

        temp = node->left;
        successor->left = temp;
        rb_set_parent(temp, successor);

        parent_meta = node->parent_meta;
        temp = rb_node_parent(node);
        rb_change_child(node, successor, temp, root);

        if (successor_child) {
            rb_set_parent_and_color(successor_child, parent, RB_COLOR_BLACK);
            rebalance = nullptr;
        } else {
            rebalance = rb_node_is_black(successor) ? parent : nullptr;
        }

        // The successor inherits the removed node's parent link and color.
        successor->parent_meta = parent_meta;
        temp = successor;
    }

    ops->propagate(temp, nullptr);
    return rebalance;
}

/*
 * Balance the tree after linking 'node', maintaining the aggregated value.
 *
 * The freshly linked leaf is recomputed on its own first (the 'until' equal to
 * its parent guarantees a single pass that ignores the propagate early-abort),
 * then the change is propagated up through the ancestors.
 */
static ALWAYS_INLINE void rb_node_balance_aggregated(
    struct rb_node *node, struct rb_root *root,
    const struct rb_tree_aggregated_ops *ops
)
{
    struct rb_node *parent;

    parent = rb_node_parent(node);
    ops->propagate(node, parent);
    ops->propagate(parent, nullptr);
    rb_node_balance_after_insert(node, root, ops->rotate);
}

static ALWAYS_INLINE void rb_node_balance_aggregated_cached(
    struct rb_node *node, struct rb_root_cached *root, bool is_leftmost,
    const struct rb_tree_aggregated_ops *ops
)
{
    if (is_leftmost)
        root->left_most = node;
    rb_node_balance_aggregated(node, &root->base, ops);
}

static ALWAYS_INLINE void rb_node_insert_at_aggregated(
    struct rb_node *node, struct rb_node *parent,
    struct rb_node **parent_link, struct rb_root *root,
    const struct rb_tree_aggregated_ops *ops
)
{
    rb_node_link(node, parent, parent_link);
    rb_node_balance_aggregated(node, root, ops);
}

static ALWAYS_INLINE void rb_node_insert_at_aggregated_cached(
    struct rb_node *node, struct rb_node *parent,
    struct rb_node **parent_link, struct rb_root_cached *root,
    bool is_leftmost, const struct rb_tree_aggregated_ops *ops
)
{
    rb_node_link(node, parent, parent_link);
    rb_node_balance_aggregated_cached(node, root, is_leftmost, ops);
}

static ALWAYS_INLINE void rb_node_insert_aggregated(
    struct rb_node *node, struct rb_root *root,
    rb_node_cmp_less_cb_t cmp_cb, const struct rb_tree_aggregated_ops *ops
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

    rb_node_insert_at_aggregated(node, parent, parent_link, root, ops);
}

static ALWAYS_INLINE struct rb_node *rb_node_find_or_insert_aggregated(
    struct rb_node *node, struct rb_root *root,
    rb_node_cmp_threeway_cb_t cmp_cb,
    const struct rb_tree_aggregated_ops *ops
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

    rb_node_insert_at_aggregated(node, parent, parent_link, root, ops);
    return nullptr;
}

static ALWAYS_INLINE void rb_node_insert_aggregated_cached(
    struct rb_node *node, struct rb_root_cached *root,
    rb_node_cmp_less_cb_t cmp_cb, const struct rb_tree_aggregated_ops *ops
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

    rb_node_insert_at_aggregated_cached(
        node, parent, parent_link, root, is_leftmost, ops
    );
}

static ALWAYS_INLINE struct rb_node *rb_node_find_or_insert_aggregated_cached(
    struct rb_node *node, struct rb_root_cached *root,
    rb_node_cmp_threeway_cb_t cmp_cb,
    const struct rb_tree_aggregated_ops *ops
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

    rb_node_insert_at_aggregated_cached(
        node, parent, parent_link, root, is_leftmost, ops
    );
    return nullptr;
}

/*
 * Remove 'node' from the tree while keeping the aggregated value up to date.
 * The fast inline path mirrors the rest of the tree: splice the node out, and
 * only run the color fixup if a black node was actually removed.
 */
static ALWAYS_INLINE void rb_node_remove_aggregated(
    struct rb_node *node, struct rb_root *root,
    const struct rb_tree_aggregated_ops *ops
)
{
    struct rb_node *rebalance;

    rebalance = rb_node_remove_and_compute_rebalance(node, root, ops);
    if (rebalance)
        rb_node_balance_after_remove(rebalance, root, ops->rotate);
}

static ALWAYS_INLINE void rb_node_remove_aggregated_cached(
    struct rb_node *node, struct rb_root_cached *root,
    const struct rb_tree_aggregated_ops *ops
)
{
    if (node == root->left_most)
        root->left_most = rb_next(node);

    rb_node_remove_aggregated(node, &root->base, ops);
}

/*
 * Replace 'old' with 'new', which is expected to occupy the exact same slot
 * (i.e. compare equal). 'new' inherits the aggregated value of 'old'.
 */
static ALWAYS_INLINE void rb_node_replace_aggregated(
    struct rb_node *old, struct rb_node *new, struct rb_root *root,
    const struct rb_tree_aggregated_ops *ops
)
{
    rb_node_replace(old, new, root);
    ops->copy(old, new);
}

static ALWAYS_INLINE void rb_node_replace_aggregated_cached(
    struct rb_node *old, struct rb_node *new, struct rb_root_cached *root,
    const struct rb_tree_aggregated_ops *ops
)
{
    if (old == root->left_most)
        root->left_most = new;

    rb_node_replace_aggregated(old, new, &root->base, ops);
}

/*
 * Define ops for an aggregated red-black tree
 *
 * An aggregated red-black tree is a red-black tree where each node contains an
 * aggregated value about all of its children. The aggregated value is
 * maintained up to date via a set of callbacks that are defined by this macro.
 *
 * 'ops_prefix'            - Prefix used for the ops struct (e.g., static)
 * 'name'                  - Name of the aggregated red-black tree
 * 'container_struct'      - Parent struct holding both the rb_node & value
 * 'rb_node_field'         - Field name of the rb_node structure inside parent
 * 'aggregated_field'      - Field name of the aggregated value inside parent
 * 'aggregated_compute_fn' - Function that computes the aggregated value.
 *                           Returns TRUE if the value changed, FALSE if it
 *                           stayed the same (triggering the early-abort).
 */
#define AGGREGATED_RB_TREE_OPS(                                               \
    ops_prefix, name, container_struct, rb_node_field,                        \
    aggregated_field, aggregated_compute_fn                                   \
)                                                                             \
static inline void name##_propagate(                                          \
    struct rb_node *from, struct rb_node *until                               \
)                                                                             \
{                                                                             \
    container_struct *node;                                                   \
                                                                              \
    while (from != until) {                                                   \
        node = rb_entry(from, container_struct, rb_node_field);               \
                                                                              \
        if (!aggregated_compute_fn(node))                                     \
            break;                                                            \
                                                                              \
        from = rb_node_parent(from);                                          \
    }                                                                         \
}                                                                             \
                                                                              \
static inline void name##_rotate(struct rb_node *old, struct rb_node *new)    \
{                                                                             \
    container_struct *old_node, *new_node;                                    \
                                                                              \
    old_node = rb_entry(old, container_struct, rb_node_field);                \
    new_node = rb_entry(new, container_struct, rb_node_field);                \
                                                                              \
    new_node->aggregated_field = old_node->aggregated_field;                  \
    aggregated_compute_fn(old_node);                                          \
}                                                                             \
                                                                              \
static inline void name##_copy(struct rb_node *from, struct rb_node *to)      \
{                                                                             \
    container_struct *from_node, *to_node;                                    \
                                                                              \
    from_node = rb_entry(from, container_struct, rb_node_field);              \
    to_node = rb_entry(to, container_struct, rb_node_field);                  \
                                                                              \
    to_node->aggregated_field = from_node->aggregated_field;                  \
}                                                                             \
                                                                              \
ops_prefix const struct rb_tree_aggregated_ops name##_ops = {                 \
    .propagate = name##_propagate,                                            \
    .rotate = name##_rotate,                                                  \
    .copy = name##_copy,                                                      \
}

/*
 * Define ops for an aggregated rbtree where the aggregated value is computed
 * as the maximum subtree value of all children. Typically this includes
 * interval trees or similar
 */
#define AGGREGATED_SUBTREE_MAX_RBTREE_OPS(                                    \
    ops_prefix, name, container_struct, rb_node_field, subtree_max_field,     \
    compute_for_this_node_fn                                                  \
)                                                                             \
static inline bool name##_compute_subtree_max(container_struct *node)         \
{                                                                             \
    container_struct *child;                                                  \
    typeof(node->subtree_max_field) subtree_max;                              \
    struct rb_node *rb = &node->rb_node_field;                                \
    bool did_change;                                                          \
                                                                              \
    subtree_max = compute_for_this_node_fn(node);                             \
                                                                              \
    if (rb->left) {                                                           \
        child = rb_entry(rb->left, container_struct, rb_node_field);          \
        subtree_max = MAX(subtree_max, child->subtree_max_field);             \
    }                                                                         \
                                                                              \
    if (rb->right) {                                                          \
        child = rb_entry(rb->right, container_struct, rb_node_field);         \
        subtree_max = MAX(subtree_max, child->subtree_max_field);             \
    }                                                                         \
                                                                              \
    did_change = node->subtree_max_field != subtree_max;                      \
    node->subtree_max_field = subtree_max;                                    \
                                                                              \
    return did_change;                                                        \
}                                                                             \
                                                                              \
AGGREGATED_RB_TREE_OPS(                                                       \
    ops_prefix, name, container_struct, rb_node_field,                        \
    subtree_max_field, name##_compute_subtree_max                             \
)
