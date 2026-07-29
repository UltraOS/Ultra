#include <common/rb_tree_aggregated.h>

/*
 * The plain (non-aggregated) tree is the aggregated tree driven by a set of
 * callbacks that do nothing. Because the rebalancing cores below are
 * ALWAYS_INLINE and this ops set is a compile-time constant, the compiler is
 * able to fold the callbacks away entirely, leaving the plain tree with zero
 * aggregation overhead.
 */
static void rb_no_aggregation_propagate(
    struct rb_node *from, struct rb_node *until
)
{
    UNREFERENCED_PARAMETER(from);
    UNREFERENCED_PARAMETER(until);
}

static void rb_no_aggregation_rotate(struct rb_node *old, struct rb_node *new)
{
    UNREFERENCED_PARAMETER(old);
    UNREFERENCED_PARAMETER(new);
}

static void rb_no_aggregation_copy(struct rb_node *from, struct rb_node *to)
{
    UNREFERENCED_PARAMETER(from);
    UNREFERENCED_PARAMETER(to);
}

static const struct rb_tree_aggregated_ops rb_no_aggregation_ops = {
    .propagate = rb_no_aggregation_propagate,
    .rotate = rb_no_aggregation_rotate,
    .copy = rb_no_aggregation_copy,
};

/*
 * Treat the absence of a node as black, which is the standard red-black tree
 * convention. This keeps the rebalancing logic free of null checks.
 */
static bool rb_is_red(struct rb_node *node)
{
    return node != nullptr && rb_node_is_red(node);
}

static bool rb_is_black(struct rb_node *node)
{
    return node == nullptr || rb_node_is_black(node);
}

/*
 *      N                R
 *     / \              / \
 *    a   R     ->      N   c
 *       / \          / \
 *      b   c        a   b
 *
 * 'R' takes over 'N's subtree, so the aggregated value moves with it via the
 * 'rotate' callback while 'N' recomputes its own.
 */
static ALWAYS_INLINE void rb_rotate_left(
    struct rb_node *node, struct rb_root *root,
    rb_aggregated_rotate_cb_t rotate
)
{
    struct rb_node *right, *parent;

    right = node->right;
    parent = rb_node_parent(node);

    node->right = right->left;
    if (right->left)
        rb_set_parent(right->left, node);

    right->left = node;
    rb_set_parent(right, parent);
    rb_set_parent(node, right);

    rb_change_child(node, right, parent, root);
    rotate(node, right);
}

/*
 *        N            L
 *       / \          / \
 *      L   c   ->    a   N
 *     / \              / \
 *    a   b            b   c
 */
static ALWAYS_INLINE void rb_rotate_right(
    struct rb_node *node, struct rb_root *root,
    rb_aggregated_rotate_cb_t rotate
)
{
    struct rb_node *left, *parent;

    left = node->left;
    parent = rb_node_parent(node);

    node->left = left->right;
    if (left->right)
        rb_set_parent(left->right, node);

    left->right = node;
    rb_set_parent(left, parent);
    rb_set_parent(node, left);

    rb_change_child(node, left, parent, root);
    rotate(node, left);
}

/*
 * Core insert color fixup. 'rotate' is woven into the rotations so that an
 * aggregated tree maintains its value, while the plain tree passes a no-op.
 */
static ALWAYS_INLINE void do_rb_node_balance_after_insert(
    struct rb_node *node, struct rb_root *root,
    rb_aggregated_rotate_cb_t rotate
)
{
    struct rb_node *parent, *gparent, *uncle;

    /*
     * The freshly linked node starts out red so that the black height of
     * the tree is preserved. The loop below restores the red property if
     * needed.
     */
    rb_node_set_color(node, RB_COLOR_RED);

    while ((parent = rb_node_parent(node)) && rb_node_is_red(parent)) {
        /*
         * A red parent can never be the root (the root is always black), so a
         * grandparent is guaranteed to exist here.
         */
        gparent = rb_node_parent(parent);

        if (parent == gparent->left) {
            uncle = gparent->right;

            if (rb_is_red(uncle)) {
                rb_node_set_color(parent, RB_COLOR_BLACK);
                rb_node_set_color(uncle, RB_COLOR_BLACK);
                rb_node_set_color(gparent, RB_COLOR_RED);
                node = gparent;
                continue;
            }

            if (node == parent->right) {
                rb_rotate_left(parent, root, rotate);
                node = parent;
                parent = rb_node_parent(node);
            }

            rb_node_set_color(parent, RB_COLOR_BLACK);
            rb_node_set_color(gparent, RB_COLOR_RED);
            rb_rotate_right(gparent, root, rotate);
        } else {
            uncle = gparent->left;

            if (rb_is_red(uncle)) {
                rb_node_set_color(parent, RB_COLOR_BLACK);
                rb_node_set_color(uncle, RB_COLOR_BLACK);
                rb_node_set_color(gparent, RB_COLOR_RED);
                node = gparent;
                continue;
            }

            if (node == parent->left) {
                rb_rotate_right(parent, root, rotate);
                node = parent;
                parent = rb_node_parent(node);
            }

            rb_node_set_color(parent, RB_COLOR_BLACK);
            rb_node_set_color(gparent, RB_COLOR_RED);
            rb_rotate_left(gparent, root, rotate);
        }
    }

    rb_node_set_color(root->root, RB_COLOR_BLACK);
}

/*
 * Core removal color fixup. 'parent' is the node at which an extra black
 * needs to be resolved. The doubly-black node itself starts out null since
 * it may be an empty leaf. 'rotate' is threaded through the rotations like
 * above.
 */
static ALWAYS_INLINE void do_rb_node_balance_after_remove(
    struct rb_node *parent, struct rb_root *root,
    rb_aggregated_rotate_cb_t rotate
)
{
    struct rb_node *node = nullptr, *sibling;

    while (node != root->root && rb_is_black(node)) {
        if (node == parent->left) {
            sibling = parent->right;

            if (rb_is_red(sibling)) {
                rb_node_set_color(sibling, RB_COLOR_BLACK);
                rb_node_set_color(parent, RB_COLOR_RED);
                rb_rotate_left(parent, root, rotate);
                sibling = parent->right;
            }

            if (rb_is_black(sibling->left) &&
                rb_is_black(sibling->right)) {
                rb_node_set_color(sibling, RB_COLOR_RED);
                node = parent;
                parent = rb_node_parent(node);
                continue;
            }

            if (rb_is_black(sibling->right)) {
                rb_node_set_color(sibling->left, RB_COLOR_BLACK);
                rb_node_set_color(sibling, RB_COLOR_RED);
                rb_rotate_right(sibling, root, rotate);
                sibling = parent->right;
            }

            rb_node_set_color(sibling, rb_node_color(parent));
            rb_node_set_color(parent, RB_COLOR_BLACK);
            rb_node_set_color(sibling->right, RB_COLOR_BLACK);
            rb_rotate_left(parent, root, rotate);
            node = root->root;
            break;
        } else {
            sibling = parent->left;

            if (rb_is_red(sibling)) {
                rb_node_set_color(sibling, RB_COLOR_BLACK);
                rb_node_set_color(parent, RB_COLOR_RED);
                rb_rotate_right(parent, root, rotate);
                sibling = parent->left;
            }

            if (rb_is_black(sibling->left) &&
                rb_is_black(sibling->right)) {
                rb_node_set_color(sibling, RB_COLOR_RED);
                node = parent;
                parent = rb_node_parent(node);
                continue;
            }

            if (rb_is_black(sibling->left)) {
                rb_node_set_color(sibling->right, RB_COLOR_BLACK);
                rb_node_set_color(sibling, RB_COLOR_RED);
                rb_rotate_left(sibling, root, rotate);
                sibling = parent->left;
            }

            rb_node_set_color(sibling, rb_node_color(parent));
            rb_node_set_color(parent, RB_COLOR_BLACK);
            rb_node_set_color(sibling->left, RB_COLOR_BLACK);
            rb_rotate_right(parent, root, rotate);
            node = root->root;
            break;
        }
    }

    if (node)
        rb_node_set_color(node, RB_COLOR_BLACK);
}

/*
 * Aggregation entry points, emitted once with an indirect rotate callback and
 * reused by the inline aggregated wrappers in the header.
 */
void rb_node_balance_after_insert(
    struct rb_node *node, struct rb_root *root,
    rb_aggregated_rotate_cb_t rotate
)
{
    do_rb_node_balance_after_insert(node, root, rotate);
}

void rb_node_balance_after_remove(
    struct rb_node *parent, struct rb_root *root,
    rb_aggregated_rotate_cb_t rotate
)
{
    do_rb_node_balance_after_remove(parent, root, rotate);
}

void rb_node_balance(struct rb_node *node, struct rb_root *root)
{
    do_rb_node_balance_after_insert(node, root, rb_no_aggregation_rotate);
}

void rb_node_remove(struct rb_node *node, struct rb_root *root)
{
    struct rb_node *rebalance;

    rebalance = rb_node_remove_and_compute_rebalance(
        node, root, &rb_no_aggregation_ops
    );
    if (!rebalance)
        return;

    do_rb_node_balance_after_remove(rebalance, root, rb_no_aggregation_rotate);
}

void rb_node_replace(
    struct rb_node *old, struct rb_node *new, struct rb_root *root
)
{
    struct rb_node *parent;

    parent = rb_node_parent(old);

    // Copies parent_meta (parent + color), left and right verbatim.
    *new = *old;

    if (old->left)
        rb_set_parent(old->left, new);
    if (old->right)
        rb_set_parent(old->right, new);

    rb_change_child(old, new, parent, root);
}

struct rb_node *rb_next(struct rb_node *node)
{
    struct rb_node *parent;

    /*
     * With a right subtree the successor is its left-most node, otherwise it
     * is the nearest ancestor for which 'node' lies in the left subtree.
     */
    if (node->right) {
        node = node->right;
        while (node->left)
            node = node->left;
        return node;
    }

    while ((parent = rb_node_parent(node)) && node == parent->right)
        node = parent;

    return parent;
}

struct rb_node *rb_prev(struct rb_node *node)
{
    struct rb_node *parent;

    if (node->left) {
        node = node->left;
        while (node->right)
            node = node->right;
        return node;
    }

    while ((parent = rb_node_parent(node)) && node == parent->left)
        node = parent;

    return parent;
}
