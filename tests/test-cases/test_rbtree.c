#include <kernel-source/common/rb_tree.c>

#include <test_harness.h>

struct rb_item {
    int key;
    struct rb_node node;
};

static bool item_less(const struct rb_node *a, const struct rb_node *b)
{
    return rb_entry(a, struct rb_item, node)->key <
           rb_entry(b, struct rb_item, node)->key;
}

static int item_cmp(const struct rb_node *a, const struct rb_node *b)
{
    int ka = rb_entry(a, struct rb_item, node)->key;
    int kb = rb_entry(b, struct rb_item, node)->key;

    return (ka > kb) - (ka < kb);
}

static int item_key_cmp(const void *key, const struct rb_node *n)
{
    int k = *(const int*)key;
    int nk = rb_entry(n, struct rb_item, node)->key;

    return (k > nk) - (k < nk);
}

static int item_key(struct rb_node *n)
{
    return rb_entry(n, struct rb_item, node)->key;
}

/*
 * Recursively assert the structural red-black invariants for the subtree at
 * 'n' and return its black height (counting null leaves as black):
 *  - every node points back at its real parent
 *  - a red node never has a red child
 *  - both children have an identical black height
 */
static int rb_check(struct rb_node *n, struct rb_node *parent)
{
    int lh, rh;

    if (n == nullptr)
        return 1;

    ASSERT_EQ(rb_node_parent(n), parent);

    if (rb_node_is_red(n)) {
        ASSERT(n->left == nullptr || rb_node_is_black(n->left));
        ASSERT(n->right == nullptr || rb_node_is_black(n->right));
    }

    lh = rb_check(n->left, n);
    rh = rb_check(n->right, n);
    ASSERT_EQ(lh, rh);

    return lh + (rb_node_is_black(n) ? 1 : 0);
}

/*
 * Assert every tree-wide invariant: the root is black, the structural rules
 * hold, forward (rb_first/rb_next) and reverse (rb_last/rb_prev) traversals
 * are strictly sorted, and both visit exactly 'expect' nodes.
 */
static void rb_validate(struct rb_root *root, size_t expect)
{
    struct rb_node *n, *prev = nullptr;
    size_t count = 0;

    if (root->root)
        ASSERT(rb_node_is_black(root->root));

    rb_check(root->root, nullptr);

    for (n = rb_first(root); n; n = rb_next(n)) {
        if (prev)
            ASSERT(item_key(prev) < item_key(n));
        prev = n;
        count++;
    }
    ASSERT_EQ(count, expect);

    count = 0;
    prev = nullptr;
    for (n = rb_last(root); n; n = rb_prev(n)) {
        if (prev)
            ASSERT(item_key(prev) > item_key(n));
        prev = n;
        count++;
    }
    ASSERT_EQ(count, expect);
}

TEST_CASE(rb_tree_empty)
{
    struct rb_root root = RB_ROOT_INIT;

    ASSERT(root.root == nullptr);
    ASSERT(rb_first(&root) == nullptr);
    ASSERT(rb_last(&root) == nullptr);
    rb_validate(&root, 0);
}

TEST_CASE(rb_tree_single_insert)
{
    struct rb_root root = RB_ROOT_INIT;
    struct rb_item a = { .key = 42 };

    rb_node_insert(&a.node, &root, item_less);

    ASSERT_EQ(root.root, &a.node);
    ASSERT(rb_node_parent(&a.node) == nullptr);
    // The root is always black.
    ASSERT(rb_node_is_black(&a.node));
    ASSERT_EQ(rb_first(&root), &a.node);
    ASSERT_EQ(rb_last(&root), &a.node);
    rb_validate(&root, 1);
}

TEST_CASE(rb_tree_insert_sorted_traversal)
{
    struct rb_root root = RB_ROOT_INIT;
    // Deliberately unsorted insertion order.
    int keys[] = { 5, 1, 9, 3, 7, 0, 8, 2, 6, 4 };
    struct rb_item items[ARRAY_SIZE(keys)];
    struct rb_node *n;
    int expected, i;

    for (i = 0; i < (int)ARRAY_SIZE(keys); i++) {
        items[i].key = keys[i];
        rb_node_insert(&items[i].node, &root, item_less);
    }

    rb_validate(&root, ARRAY_SIZE(keys));

    expected = 0;
    for (n = rb_first(&root); n; n = rb_next(n)) {
        ASSERT_EQ(item_key(n), expected);
        expected++;
    }
    ASSERT_EQ(expected, (int)ARRAY_SIZE(keys));

    ASSERT_EQ(item_key(rb_first(&root)), 0);
    ASSERT_EQ(item_key(rb_last(&root)), 9);
}

TEST_CASE(rb_tree_find)
{
    struct rb_root root = RB_ROOT_INIT;
    struct rb_item items[16];
    int i, key;

    for (i = 0; i < (int)ARRAY_SIZE(items); i++) {
        items[i].key = i * 2;
        rb_node_insert(&items[i].node, &root, item_less);
    }

    for (i = 0; i < (int)ARRAY_SIZE(items); i++) {
        key = i * 2;
        ASSERT_EQ(rb_node_find(&key, &root, item_key_cmp), &items[i].node);
    }

    // Odd keys were never inserted.
    key = 7;
    ASSERT(rb_node_find(&key, &root, item_key_cmp) == nullptr);
    key = -1;
    ASSERT(rb_node_find(&key, &root, item_key_cmp) == nullptr);
}

TEST_CASE(rb_tree_find_or_insert)
{
    struct rb_root root = RB_ROOT_INIT;
    struct rb_item a = { .key = 10 };
    struct rb_item b = { .key = 20 };
    struct rb_item dup = { .key = 10 };
    struct rb_node *ret;

    // First insertion of a fresh key returns nullptr.
    ASSERT(rb_node_find_or_insert(&a.node, &root, item_cmp) == nullptr);
    ASSERT(rb_node_find_or_insert(&b.node, &root, item_cmp) == nullptr);

    // A duplicate key returns the already-present node and does not insert.
    ret = rb_node_find_or_insert(&dup.node, &root, item_cmp);
    ASSERT_EQ(ret, &a.node);

    rb_validate(&root, 2);
}

TEST_CASE(rb_tree_remove_leaf)
{
    struct rb_root root = RB_ROOT_INIT;
    struct rb_item items[7];
    int keys[] = { 4, 2, 6, 1, 3, 5, 7 };
    int i, key;

    for (i = 0; i < (int)ARRAY_SIZE(keys); i++) {
        items[i].key = keys[i];
        rb_node_insert(&items[i].node, &root, item_less);
    }
    rb_validate(&root, ARRAY_SIZE(keys));

    // items[3] holds key 1, a leaf in this layout.
    rb_node_remove(&items[3].node, &root);
    rb_validate(&root, ARRAY_SIZE(keys) - 1);

    key = 1;
    ASSERT(rb_node_find(&key, &root, item_key_cmp) == nullptr);
}

TEST_CASE(rb_tree_remove_root)
{
    struct rb_root root = RB_ROOT_INIT;
    struct rb_item a = { .key = 1 };

    rb_node_insert(&a.node, &root, item_less);
    rb_node_remove(&a.node, &root);

    ASSERT(root.root == nullptr);
    rb_validate(&root, 0);
}

TEST_CASE(rb_tree_remove_two_children)
{
    struct rb_root root = RB_ROOT_INIT;
    struct rb_item items[7];
    int keys[] = { 4, 2, 6, 1, 3, 5, 7 };
    struct rb_node *found;
    int i, key;

    for (i = 0; i < (int)ARRAY_SIZE(keys); i++) {
        items[i].key = keys[i];
        rb_node_insert(&items[i].node, &root, item_less);
    }

    // items[0] holds key 4 (the root) and has two children.
    rb_node_remove(&items[0].node, &root);
    rb_validate(&root, ARRAY_SIZE(keys) - 1);

    key = 4;
    ASSERT(rb_node_find(&key, &root, item_key_cmp) == nullptr);

    // The successor (key 5) must still be reachable.
    key = 5;
    found = rb_node_find(&key, &root, item_key_cmp);
    ASSERT(found != nullptr);
    ASSERT_EQ(item_key(found), 5);
}

TEST_CASE(rb_tree_remove_all_sequential)
{
    struct rb_root root = RB_ROOT_INIT;
    enum { N = 64 };
    struct rb_item items[N];
    int i;

    for (i = 0; i < N; i++) {
        items[i].key = i;
        rb_node_insert(&items[i].node, &root, item_less);
    }
    rb_validate(&root, N);

    // Remove from the front, the tree must stay balanced throughout.
    for (i = 0; i < N; i++) {
        rb_node_remove(&items[i].node, &root);
        rb_validate(&root, N - i - 1);
    }

    ASSERT(root.root == nullptr);
}

TEST_CASE(rb_tree_replace)
{
    struct rb_root root = RB_ROOT_INIT;
    struct rb_item items[5];
    int keys[] = { 2, 1, 4, 3, 5 };
    struct rb_item replacement = { .key = 4 };
    struct rb_node *old, *found;
    enum rb_color old_color;
    int i, key;

    for (i = 0; i < (int)ARRAY_SIZE(keys); i++) {
        items[i].key = keys[i];
        rb_node_insert(&items[i].node, &root, item_less);
    }

    // items[2] holds key 4 (has children 3 and 5).
    old = &items[2].node;
    old_color = rb_node_color(old);

    rb_node_replace(old, &replacement.node, &root);

    // Structure and color must be preserved verbatim.
    ASSERT_EQ(rb_node_color(&replacement.node), old_color);
    rb_validate(&root, ARRAY_SIZE(keys));

    key = 4;
    found = rb_node_find(&key, &root, item_key_cmp);
    ASSERT_EQ(found, &replacement.node);
}

TEST_CASE(rb_tree_insert_cached)
{
    struct rb_root_cached root = RB_ROOT_CACHED_INIT;
    int keys[] = { 5, 3, 8, 1, 9, 0, 7 };
    struct rb_item items[ARRAY_SIZE(keys)];
    int i;

    for (i = 0; i < (int)ARRAY_SIZE(keys); i++) {
        items[i].key = keys[i];
        rb_node_insert_cached(&items[i].node, &root, item_less);

        // The cached left-most must always match a fresh lookup.
        ASSERT_EQ(rb_first_cached(&root), rb_first(&root.base));
    }

    rb_validate(&root.base, ARRAY_SIZE(keys));
    ASSERT_EQ(item_key(rb_first_cached(&root)), 0);
}

TEST_CASE(rb_tree_replace_cached_leftmost)
{
    struct rb_root_cached root = RB_ROOT_CACHED_INIT;
    struct rb_item items[3];
    int keys[] = { 2, 1, 3 };
    struct rb_item replacement = { .key = 1 };
    int i;

    for (i = 0; i < (int)ARRAY_SIZE(keys); i++) {
        items[i].key = keys[i];
        rb_node_insert_cached(&items[i].node, &root, item_less);
    }

    // items[1] holds key 1, the cached left-most node.
    ASSERT_EQ(rb_first_cached(&root), &items[1].node);

    rb_node_replace_cached(&items[1].node, &replacement.node, &root);

    // The cache must now follow the replacement node.
    ASSERT_EQ(rb_first_cached(&root), &replacement.node);
    ASSERT_EQ(rb_first(&root.base), &replacement.node);
    rb_validate(&root.base, ARRAY_SIZE(keys));
}

/* Deterministic xorshift so the stress test is reproducible across runs. */
static u32 g_rng = 0x9e3779b9u;

static u32 rng_next(void)
{
    u32 x = g_rng;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng = x;

    return x;
}

TEST_CASE(rb_tree_randomized_stress)
{
    struct rb_root root = RB_ROOT_INIT;
    enum { N = 512, ITERS = 40000 };
    static struct rb_item items[N];
    static bool present[N];
    size_t live = 0;
    int i, iter;

    for (i = 0; i < N; i++) {
        items[i].key = i;
        present[i] = false;
    }

    for (iter = 0; iter < ITERS; iter++) {
        int idx = rng_next() % N;

        if (!present[idx]) {
            rb_node_insert(&items[idx].node, &root, item_less);
            present[idx] = true;
            live++;
        } else {
            rb_node_remove(&items[idx].node, &root);
            present[idx] = false;
            live--;
        }

        // Validating every iteration is O(n) but keeps the search tight.
        if ((iter % 200) == 0)
            rb_validate(&root, live);

        if ((iter % 137) == 0) {
            int key = idx;
            struct rb_node *f = rb_node_find(&key, &root, item_key_cmp);

            if (present[idx])
                ASSERT_EQ(f, &items[idx].node);
            else
                ASSERT(f == nullptr);
        }
    }

    // Drain whatever is left and confirm we end up empty.
    for (i = 0; i < N; i++) {
        if (present[i]) {
            rb_node_remove(&items[i].node, &root);
            present[i] = false;
            live--;
        }
    }

    rb_validate(&root, 0);
    ASSERT(root.root == nullptr);
}

/*
 * Aggregated tree tests. Each node caches the size of its own subtree, which
 * is both easy to validate (the root's value equals the node count) and
 * exercises every callback: propagate (insert/remove), rotate (rebalancing)
 * and copy (successor splice / replace).
 */
struct aug_item {
    int key;
    size_t subtree_size;
    struct rb_node node;
};

static bool aug_compute(struct aug_item *n)
{
    size_t size = 1;

    if (n->node.left)
        size += rb_entry(n->node.left, struct aug_item, node)->subtree_size;
    if (n->node.right)
        size += rb_entry(n->node.right, struct aug_item, node)->subtree_size;

    if (size == n->subtree_size)
        return false;

    n->subtree_size = size;
    return true;
}

AGGREGATED_RB_TREE_OPS(
    static, aug, struct aug_item, node, subtree_size, aug_compute
);

static bool aug_less(const struct rb_node *a, const struct rb_node *b)
{
    return rb_entry(a, struct aug_item, node)->key <
           rb_entry(b, struct aug_item, node)->key;
}

static int aug_key_cmp(const void *key, const struct rb_node *n)
{
    int k = *(const int*)key;
    int nk = rb_entry(n, struct aug_item, node)->key;

    return (k > nk) - (k < nk);
}

static int aug_item_key(struct rb_node *n)
{
    return rb_entry(n, struct aug_item, node)->key;
}

static size_t aug_node_size(struct rb_node *n)
{
    return n ? rb_entry(n, struct aug_item, node)->subtree_size : 0;
}

/*
 * Assert the red-black structure and that every cached subtree size matches
 * the real number of nodes underneath it. Returns the actual subtree size.
 */
static size_t aug_check(struct rb_node *n, struct rb_node *parent)
{
    size_t total;

    if (n == nullptr)
        return 0;

    ASSERT_EQ(rb_node_parent(n), parent);

    if (rb_node_is_red(n)) {
        ASSERT(n->left == nullptr || rb_node_is_black(n->left));
        ASSERT(n->right == nullptr || rb_node_is_black(n->right));
    }

    total = aug_check(n->left, n) + aug_check(n->right, n) + 1;
    ASSERT_EQ(aug_node_size(n), total);

    return total;
}

static void aug_validate(struct rb_root *root, size_t expect)
{
    struct rb_node *n, *prev = nullptr;
    size_t count = 0;

    if (root->root)
        ASSERT(rb_node_is_black(root->root));

    ASSERT_EQ(aug_check(root->root, nullptr), expect);

    for (n = rb_first(root); n; n = rb_next(n)) {
        if (prev)
            ASSERT(aug_item_key(prev) < aug_item_key(n));
        prev = n;
        count++;
    }
    ASSERT_EQ(count, expect);
}

/* Order-statistic lookup of the 'k'-th (0-indexed) smallest via cached sizes. */
static struct rb_node *aug_select(struct rb_root *root, size_t k)
{
    struct rb_node *n = root->root;

    while (n) {
        size_t left_size = aug_node_size(n->left);

        if (k < left_size) {
            n = n->left;
        } else if (k > left_size) {
            k -= left_size + 1;
            n = n->right;
        } else {
            return n;
        }
    }

    return nullptr;
}

TEST_CASE(rb_aggregated_subtree_sizes)
{
    struct rb_root root = RB_ROOT_INIT;
    int keys[] = { 5, 1, 9, 3, 7, 0, 8, 2, 6, 4 };
    struct aug_item items[ARRAY_SIZE(keys)];
    size_t i;

    for (i = 0; i < ARRAY_SIZE(keys); i++) {
        items[i].key = keys[i];
        items[i].subtree_size = 0;
        rb_node_insert_aggregated(&items[i].node, &root, aug_less, &aug_ops);
        aug_validate(&root, i + 1);
    }

    ASSERT_EQ(aug_node_size(root.root), ARRAY_SIZE(keys));
}

TEST_CASE(rb_aggregated_order_statistic)
{
    struct rb_root root = RB_ROOT_INIT;
    enum { N = 50 };
    struct aug_item items[N];
    size_t k;
    int i;

    // Insert in reverse so the tree is forced to rebalance heavily.
    for (i = N - 1; i >= 0; i--) {
        items[i].key = i;
        items[i].subtree_size = 0;
        rb_node_insert_aggregated(&items[i].node, &root, aug_less, &aug_ops);
    }
    aug_validate(&root, N);

    // The k-th smallest must have key k for this dense, contiguous key set.
    for (k = 0; k < N; k++)
        ASSERT_EQ(aug_item_key(aug_select(&root, k)), (int)k);
}

TEST_CASE(rb_aggregated_remove)
{
    struct rb_root root = RB_ROOT_INIT;
    enum { N = 32 };
    struct aug_item items[N];
    int i, key;

    for (i = 0; i < N; i++) {
        items[i].key = i;
        items[i].subtree_size = 0;
        rb_node_insert_aggregated(&items[i].node, &root, aug_less, &aug_ops);
    }
    aug_validate(&root, N);

    // Remove every other node and confirm sizes stay consistent.
    for (i = 0; i < N; i += 2) {
        rb_node_remove_aggregated(&items[i].node, &root, &aug_ops);
        key = i;
        ASSERT(rb_node_find(&key, &root, aug_key_cmp) == nullptr);
    }
    aug_validate(&root, N / 2);

    for (i = 1; i < N; i += 2)
        rb_node_remove_aggregated(&items[i].node, &root, &aug_ops);

    aug_validate(&root, 0);
    ASSERT(root.root == nullptr);
}

TEST_CASE(rb_aggregated_replace)
{
    struct rb_root root = RB_ROOT_INIT;
    struct aug_item items[5];
    int keys[] = { 2, 1, 4, 3, 5 };
    struct aug_item replacement = { .key = 4, .subtree_size = 0 };
    struct rb_node *found;
    size_t i;
    int key;

    for (i = 0; i < ARRAY_SIZE(keys); i++) {
        items[i].key = keys[i];
        items[i].subtree_size = 0;
        rb_node_insert_aggregated(&items[i].node, &root, aug_less, &aug_ops);
    }

    // items[2] holds key 4 with two children, so it caches a subtree size > 1.
    rb_node_replace_aggregated(&items[2].node, &replacement.node, &root,
                               &aug_ops);

    ASSERT_EQ(replacement.subtree_size, items[2].subtree_size);
    aug_validate(&root, ARRAY_SIZE(keys));

    key = 4;
    found = rb_node_find(&key, &root, aug_key_cmp);
    ASSERT_EQ(found, &replacement.node);
}

TEST_CASE(rb_aggregated_cached)
{
    struct rb_root_cached root = RB_ROOT_CACHED_INIT;
    int keys[] = { 5, 3, 8, 1, 9, 0, 7 };
    struct aug_item items[ARRAY_SIZE(keys)];
    size_t i;

    for (i = 0; i < ARRAY_SIZE(keys); i++) {
        items[i].key = keys[i];
        items[i].subtree_size = 0;
        rb_node_insert_aggregated_cached(&items[i].node, &root, aug_less,
                                         &aug_ops);
        ASSERT_EQ(rb_first_cached(&root), rb_first(&root.base));
    }

    aug_validate(&root.base, ARRAY_SIZE(keys));
    ASSERT_EQ(aug_item_key(rb_first_cached(&root)), 0);

    // Removing the cached left-most keeps both the cache and sizes correct.
    rb_node_remove_aggregated_cached(rb_first_cached(&root), &root, &aug_ops);
    ASSERT_EQ(aug_item_key(rb_first_cached(&root)), 1);
    aug_validate(&root.base, ARRAY_SIZE(keys) - 1);
}

TEST_CASE(rb_aggregated_randomized_stress)
{
    struct rb_root root = RB_ROOT_INIT;
    enum { N = 512, ITERS = 40000 };
    static struct aug_item items[N];
    static bool present[N];
    size_t live = 0;
    int i, iter;

    for (i = 0; i < N; i++) {
        items[i].key = i;
        items[i].subtree_size = 0;
        present[i] = false;
    }

    for (iter = 0; iter < ITERS; iter++) {
        int idx = rng_next() % N;

        if (!present[idx]) {
            rb_node_insert_aggregated(&items[idx].node, &root, aug_less,
                                      &aug_ops);
            present[idx] = true;
            live++;
        } else {
            rb_node_remove_aggregated(&items[idx].node, &root, &aug_ops);
            present[idx] = false;
            live--;
        }

        if ((iter % 200) == 0) {
            aug_validate(&root, live);

            // Spot-check the order statistic against a linear scan.
            if (live) {
                size_t k = rng_next() % live;
                struct rb_node *sel = aug_select(&root, k);
                struct rb_node *walk = rb_first(&root);

                while (k--)
                    walk = rb_next(walk);

                ASSERT_EQ(sel, walk);
            }
        }
    }

    for (i = 0; i < N; i++) {
        if (present[i]) {
            rb_node_remove_aggregated(&items[i].node, &root, &aug_ops);
            present[i] = false;
            live--;
        }
    }

    aug_validate(&root, 0);
    ASSERT(root.root == nullptr);
}
