#include <type_traits>
#include <string>

template <typename ColorT>
std::enable_if_t<std::is_enum_v<ColorT> && std::is_same_v<decltype(ColorT::BLACK), decltype(ColorT::RED)>, std::string>
serialize(ColorT color)
{
    switch (color)
    {
    case ColorT::RED:
        return "RED";
    case ColorT::BLACK:
        return "BLACK";
    default:
        return "UNKNOWN";
    }
}

#include "TestRunner.h"

// Important to be able to verify the tree structure
#define private public
#include "Common/RedBlackTree.h"
#include "Common/Set.h"
#include "Memory/Range.h"

template <typename T, typename Compare = kernel::Less<T>>
using rbtree = kernel::detail::RedBlackTree<kernel::detail::SetTraits<T, Compare, false>>;

template <typename TreeNodeT>
void recursive_verify_one(TreeNodeT parent, TreeNodeT child, size_t& counter)
{
    ++counter;
    Assert::that(child->parent).is_equal(parent);

    if (child->is_left_child()) {
        Assert::that(parent->left).is_equal(child);
        Assert::that(parent->value).is_greater_than(child->value);
    } else {
        Assert::that(parent->right).is_equal(child);
        Assert::that(child->value).is_greater_than(parent->value);
    }

    if (child->color == std::remove_pointer_t<TreeNodeT>::Color::RED)
        Assert::that(child->color).is_not_equal(parent->color);

    if (child->left)
        recursive_verify_one(child, child->left, counter);

    if (child->right)
        recursive_verify_one(child, child->right, counter);
}

template <typename TreeT>
void verify_tree_structure(const TreeT& tree)
{
    if (tree.empty())
        return;

    Assert::that(tree.m_root->parent).is_equal(tree.super_root_as_value_node());

    size_t counter = 1;

    if (tree.m_root->left)
        recursive_verify_one(tree.m_root, tree.m_root->left, counter);
    if (tree.m_root->right)
        recursive_verify_one(tree.m_root, tree.m_root->right, counter);

    Assert::that(tree.size()).is_equal(counter);
}

template <typename TreeNodeT>
void recursive_verify_nodes(TreeNodeT left, TreeNodeT right)
{
    Assert::that(left).is_not_null();
    Assert::that(right).is_not_null();

    Assert::that(left->value).is_equal(right->value);
    Assert::that(left->color).is_equal(right->color);

    if (left->left)
        recursive_verify_nodes(left->left, right->left);
    if (left->right)
        recursive_verify_nodes(left->right, right->right);
}

template <typename TreeT>
void verify_trees_are_equal(const TreeT& left, const TreeT& right)
{
    Assert::that(left.size()).is_equal(right.size());

    if (left.empty())
        return;

    Assert::that(left.m_root->value).is_equal(right.m_root->value);
    Assert::that(left.m_root->color).is_equal(right.m_root->color);

    if (left.m_root->left)
        recursive_verify_nodes(left.m_root->left, right.m_root->left);
    if (left.m_root->right)
        recursive_verify_nodes(left.m_root->right, right.m_root->right);
}

TEST(Emplace) {
    rbtree<size_t> tree;

    static constexpr size_t test_size = 10000;

    for (size_t i = 0; i < test_size; ++i)
        tree.emplace(i);

    for (size_t i = 0; i < test_size; ++i)
        Assert::that(tree.contains(i)).is_true();

    Assert::that(tree.size()).is_equal(test_size);

    Assert::that(tree.m_super_root.left).is_equal(tree.find_node(0));
    Assert::that(tree.m_super_root.right).is_equal(tree.find_node(9999));
    Assert::that(tree.m_root->parent).is_equal(tree.super_root_as_value_node());

    verify_tree_structure(tree);
}

struct ComparableDeletable : Deletable {
    ComparableDeletable(size_t id, size_t& counter)
        : m_id(id)
        , Deletable(counter)
    {
    }

    ComparableDeletable(ComparableDeletable&& other)
        : m_id(other.m_id), Deletable(std::move(other))
    {
        other.m_id = 0;
    }

    bool operator<(const ComparableDeletable& other) const { return m_id < other.m_id; }

private:
    size_t m_id { 0 };
};

TEST(Clear) {
    constexpr size_t test_size = 10000;

    size_t items_deleted_counter = 0;

    rbtree<ComparableDeletable> tree;

    for (size_t i = 0; i < test_size; ++i)
        tree.emplace(i, items_deleted_counter);

    tree.clear();

    Assert::that(items_deleted_counter).is_equal(test_size);
    Assert::that(tree.size()).is_equal(0);
}

TEST(Destructor) {
    constexpr size_t test_size = 10000;

    size_t items_deleted_counter = 0;

    {
        rbtree<ComparableDeletable> tree;

        for (size_t i = 0; i < test_size; ++i)
            tree.emplace(i, items_deleted_counter);
    }

    Assert::that(items_deleted_counter).is_equal(test_size);
}

TEST(CopyConstructor) {
    constexpr size_t test_size = 100;

    rbtree<size_t> original_tree;

    for (size_t i = 0; i < test_size; ++i)
        original_tree.emplace(i);

    rbtree<size_t> new_tree(original_tree);

    Assert::that(new_tree.size()).is_equal(original_tree.size());

    for (size_t i = 0; i < test_size; ++i)
        Assert::that(new_tree.contains(i)).is_true();

    verify_tree_structure(new_tree);
    verify_trees_are_equal(original_tree, new_tree);
}

TEST(CopyAssignment) {
    constexpr size_t test_size = 109;

    rbtree<size_t> original_tree;
    rbtree<size_t> new_tree;
    new_tree.emplace(1);

    for (size_t i = 0; i < test_size; ++i)
        original_tree.emplace(i);

    new_tree = original_tree;

    Assert::that(new_tree.size()).is_equal(original_tree.size());

    for (size_t i = 0; i < test_size; ++i)
        Assert::that(new_tree.contains(i)).is_true();

    verify_tree_structure(new_tree);
    verify_trees_are_equal(original_tree, new_tree);
}

TEST(MoveConstructor) {
    constexpr size_t test_size = 10;

    rbtree<size_t> original_tree;

    for (size_t i = 0; i < test_size; ++i)
        original_tree.emplace(i);

    rbtree<size_t> new_tree(kernel::move(original_tree));

    Assert::that(original_tree.size()).is_equal(0);
    Assert::that(new_tree.size()).is_equal(test_size);
}

TEST(MoveAssignment) {
    constexpr size_t test_size = 10;

    rbtree<size_t> original_tree;
    rbtree<size_t> new_tree;

    for (size_t i = 0; i < test_size; ++i)
        original_tree.emplace(i);

    new_tree = kernel::move(original_tree);

    Assert::that(original_tree.size()).is_equal(0);
    Assert::that(new_tree.size()).is_equal(test_size);
}

TEST(EmptyIterator) {
    rbtree<size_t> tree;

    size_t iterations = 0;

    for (const auto& elem : tree) {
        iterations++;
    }

    Assert::that(iterations).is_equal(0);
}

TEST(Iterator) {
    rbtree<size_t> tree;

    size_t size = 10001;

    for (size_t i = 0; i < size; ++i)
        tree.emplace(i);

    size_t i = 0;
    for (const auto& elem : tree) {
        Assert::that(elem).is_equal(i++);
    }
}

TEST(IteratorPostIncrement) {
    rbtree<size_t> tree;

    tree.emplace(1);
    tree.emplace(2);

    rbtree<size_t>::Iterator itr = tree.begin();

    Assert::that(*(itr++)).is_equal(1);
    Assert::that(*(itr++)).is_equal(2);
    Assert::that(itr).is_equal(tree.end());
}

TEST(IteratorPreIncrement) {
    rbtree<size_t> tree;

    tree.emplace(1);
    tree.emplace(2);
    tree.emplace(3);

    rbtree<size_t>::Iterator itr = tree.begin();

    Assert::that(*itr).is_equal(1);
    Assert::that(*(++itr)).is_equal(2);
    Assert::that(*(++itr)).is_equal(3);
    Assert::that(++itr).is_equal(tree.end());
}

TEST(IteratorPostDecrement) {
    rbtree<size_t> tree;

    tree.emplace(1);
    tree.emplace(2);
    tree.emplace(3);

    rbtree<size_t>::Iterator itr = tree.end();

    Assert::that(itr--).is_equal(tree.end());
    Assert::that(*(itr--)).is_equal(3);
    Assert::that(*(itr--)).is_equal(2);
    Assert::that(itr).is_equal(tree.begin());
}

TEST(IteratorPreDecrement) {
    rbtree<size_t> tree;

    tree.emplace(1);
    tree.emplace(2);
    tree.emplace(3);

    rbtree<size_t>::Iterator itr = tree.end();

    Assert::that(*(--itr)).is_equal(3);
    Assert::that(*(--itr)).is_equal(2);
    Assert::that(*(--itr)).is_equal(1);
    Assert::that(itr).is_equal(tree.begin());
}

TEST(LowerBound) {
    rbtree<size_t> tree;

    tree.emplace(10);
    tree.emplace(20);
    tree.emplace(30);
    tree.emplace(40);
    tree.emplace(50);

    auto itr1 = tree.lower_bound(30);
    Assert::that(*itr1).is_equal(30);

    auto itr2 = tree.lower_bound(15);
    Assert::that(*itr2).is_equal(20);

    auto itr3 = tree.lower_bound(100);
    Assert::that(itr3).is_equal(tree.end());
}

TEST(UpperBound) {
    rbtree<size_t> tree;

    tree.emplace(10);
    tree.emplace(20);
    tree.emplace(30);
    tree.emplace(40);
    tree.emplace(50);

    auto itr1 = tree.upper_bound(30);
    Assert::that(*itr1).is_equal(40);

    auto itr2 = tree.upper_bound(15);
    Assert::that(*itr2).is_equal(20);

    auto itr3 = tree.upper_bound(100);
    Assert::that(itr3).is_equal(tree.end());
}

TEST(RemovalCase4) {
    rbtree<int> tree;
    using Color = rbtree<int>::ValueNode::Color;

    // Case 4:
    //    10 (BLACK)
    //   /          \
    // -10 (BLACK)   30 (RED)
    //              /        \
    //            20 (BLACK)  38 (BLACK)

    tree.emplace(-10);
    tree.emplace(10);
    tree.emplace(30);
    tree.emplace(20);
    tree.emplace(38);

    tree.find_node(-10)->color = Color::BLACK;
    tree.find_node(10)->color = Color::BLACK;
    tree.find_node(30)->color = Color::RED;
    tree.find_node(20)->color = Color::BLACK;
    tree.find_node(38)->color = Color::BLACK;

    tree.remove(20);

    Assert::that(tree.find_node(-10)->color).is_equal(Color::BLACK);
    Assert::that(tree.find_node(10)->color).is_equal(Color::BLACK);
    Assert::that(tree.find_node(30)->color).is_equal(Color::BLACK);
    Assert::that(tree.find_node(38)->color).is_equal(Color::RED);

    verify_tree_structure(tree);
}

TEST(RemovalCase6) {
    rbtree<int> tree;
    using Color = rbtree<int>::ValueNode::Color;

    // Case 6:
    //    10 (BLACK)
    //   /          \
    // -10 (BLACK)   30 (BLACK)
    //              /        \
    //            25 (RED)  40 (RED)

    tree.emplace(-10);
    tree.emplace(10);
    tree.emplace(30);
    tree.emplace(25);
    tree.emplace(40);

    tree.remove(-10);

    Assert::that(tree.find_node(10)->color).is_equal(Color::BLACK);
    Assert::that(tree.find_node(30)->color).is_equal(Color::BLACK);
    Assert::that(tree.find_node(25)->color).is_equal(Color::RED);
    Assert::that(tree.find_node(40)->color).is_equal(Color::BLACK);

    verify_tree_structure(tree);
}

TEST(RemovalCase3ToCase1) {
    rbtree<int> tree;
    using Color = rbtree<int>::ValueNode::Color;

    // Case 3:
    //    10 (BLACK)
    //   /          \
    // -10 (BLACK)   30 (BLACK)
    // (deleting -10 we get to case 3 then case 1)

    tree.emplace(10);
    tree.emplace(-10);
    tree.emplace(30);

    tree.find_node(-10)->color = Color::BLACK;
    tree.find_node(30)->color = Color::BLACK;

    tree.remove(-10);

    Assert::that(tree.find_node(10)->color).is_equal(Color::BLACK);
    Assert::that(tree.find_node(30)->color).is_equal(Color::RED);

    verify_tree_structure(tree);
}

TEST(RemovalCase3ToCase5ToCase6) {
    rbtree<int> tree;

    using ValueNode = rbtree<int>::ValueNode;
    using Color = rbtree<int>::ValueNode::Color;

    // Case 3:
    //         ------ 10 (BLACK) -----------
    //        /                             \
    //      -30 (BLACK)                      50 (BLACK)
    //      /           \                   /          \
    //    -40 (BLACK)   -20 (BLACK)        /           70 (BLACK)
    //                                  30 (RED) --
    //                                  /          \
    //                                15 (BLACK)    40 (BLACK)
    // (deleting -40 we get to case 3, then to case 5, and then finally case 6)

    tree.emplace(10);

    // left side
    auto* minus_30 = tree.m_root->left = new ValueNode(-30);
    minus_30->parent = tree.m_root;
    minus_30->color = Color::BLACK;

    auto* minus_40 = minus_30->left = new ValueNode(-40);
    minus_40->parent = minus_30;
    minus_40->color = Color::BLACK;

    auto* minus_20 = minus_30->right = new ValueNode(-20);
    minus_20->parent = minus_30;
    minus_20->color = Color::BLACK;

    // right side
    auto* plus_50 = tree.m_root->right = new ValueNode(50);
    plus_50->color = Color::BLACK;
    plus_50->parent = tree.m_root;

    auto* plus_70 = plus_50->right = new ValueNode(70);
    plus_70->parent = plus_50;
    plus_70->color = Color::BLACK;

    auto* plus_30 = plus_50->left = new ValueNode(30);
    plus_30->parent = plus_50;
    plus_30->color = Color::RED;

    auto* plus_15 = plus_30->left = new ValueNode(15);
    plus_15->parent = plus_30;
    plus_15->color = Color::BLACK;

    auto* plus_40 = plus_30->right = new ValueNode(40);
    plus_40->parent = plus_30;
    plus_40->color = Color::BLACK;

    tree.m_size = 9;
    tree.m_root->parent = tree.super_root_as_value_node();
    tree.m_super_root.left = minus_40;
    tree.m_super_root.right = plus_70;

    verify_tree_structure(tree);

    tree.remove(-40);

    Assert::that(tree.m_root->parent).is_equal(tree.super_root_as_value_node());
    Assert::that(tree.find_node(30)->color).is_equal(Color::BLACK);
    Assert::that(tree.find_node(10)->color).is_equal(Color::BLACK);
    Assert::that(tree.find_node(-30)->color).is_equal(Color::BLACK);
    Assert::that(tree.find_node(15)->color).is_equal(Color::BLACK);
    Assert::that(tree.find_node(-20)->color).is_equal(Color::RED);
    Assert::that(tree.find_node(50)->color).is_equal(Color::BLACK);
    Assert::that(tree.find_node(40)->color).is_equal(Color::BLACK);
    Assert::that(tree.find_node(70)->color).is_equal(Color::BLACK);

    verify_tree_structure(tree);
}

TEST(RemovalCase2ToCase4) {
    rbtree<int> tree;

    using ValueNode = rbtree<int>::ValueNode;
    using Color = rbtree<int>::ValueNode::Color;

    // Case 2:
    //         ------ 10 (BLACK) -----------
    //        /                             \
    //      -10 (BLACK)                      40 (BLACK)
    //      /           \                  /          \
    //    -20 (BLACK)   -5 (BLACK)       20 (BLACK)    \
    //                                                 60 (RED)
    //                                               /          \
    //                                             50 (BLACK)   80 (BLACK)
    // (deleting 10 we get to case 2, then to case 5, and then finally case 6)

    tree.emplace(10);

    // left side
    auto* minus_10 = tree.m_root->left = new ValueNode(-10);
    minus_10->parent = tree.m_root;
    minus_10->color = Color::BLACK;

    auto* minus_20 = minus_10->left = new ValueNode(-20);
    minus_20->parent = minus_10;
    minus_20->color = Color::BLACK;

    auto* minus_5 = minus_10->right = new ValueNode(-5);
    minus_5->parent = minus_10;
    minus_5->color = Color::BLACK;

    // right side
    auto* plus_40 = tree.m_root->right = new ValueNode(40);
    plus_40->color = Color::BLACK;
    plus_40->parent = tree.m_root;

    auto* plus_20 = plus_40->left = new ValueNode(20);
    plus_20->parent = plus_40;
    plus_20->color = Color::BLACK;

    auto* plus_60 = plus_40->right = new ValueNode(60);
    plus_60->parent = plus_40;
    plus_60->color = Color::RED;

    auto* plus_50 = plus_60->left = new ValueNode(50);
    plus_50->parent = plus_60;
    plus_50->color = Color::BLACK;

    auto* plus_80 = plus_60->right = new ValueNode(80);
    plus_80->parent = plus_60;
    plus_80->color = Color::BLACK;

    tree.m_size = 9;
    tree.m_root->parent = tree.super_root_as_value_node();
    tree.m_super_root.left = minus_20;
    tree.m_super_root.right = plus_80;

    verify_tree_structure(tree);

    tree.remove(10);

    Assert::that(tree.m_root->parent).is_equal(tree.super_root_as_value_node());
    Assert::that(tree.find_node(20)->color).is_equal(Color::BLACK);
    Assert::that(tree.find_node(-10)->color).is_equal(Color::BLACK);
    Assert::that(tree.find_node(-20)->color).is_equal(Color::BLACK);
    Assert::that(tree.find_node(-5)->color).is_equal(Color::BLACK);
    Assert::that(tree.find_node(60)->color).is_equal(Color::BLACK);
    Assert::that(tree.find_node(40)->color).is_equal(Color::BLACK);
    Assert::that(tree.find_node(50)->color).is_equal(Color::RED);
    Assert::that(tree.find_node(80)->color).is_equal(Color::BLACK);

    verify_tree_structure(tree);
}

TEST(SuccessorIsChildOfRootRemoval) {
    rbtree<int> tree;

    using ValueNode = rbtree<int>::ValueNode;
    using Color = rbtree<int>::ValueNode::Color;

    tree.emplace(1);
    tree.emplace(-1);
    tree.emplace(2);

    tree.find_node(2)->color = Color::BLACK;
    tree.find_node(-1)->color = Color::BLACK;

    tree.remove(1);

    verify_tree_structure(tree);
    Assert::that(tree.find_node(2)->color).is_equal(Color::BLACK);
}

TEST(LotsOfRemoves) {
    rbtree<int> tree;

    static constexpr int test_size = 1000;

    for (auto i = 0; i < test_size; ++i) {
        tree.emplace(i);
    }

    for (auto i = 500; i >= 0; --i) {
        tree.remove(i);
        verify_tree_structure(tree);
    }

    Assert::that(tree.size()).is_equal(499);
    Assert::that(tree.m_super_root.left).is_equal(tree.find_node(501));
    Assert::that(tree.m_super_root.right).is_equal(tree.find_node(999));

    for (auto i = 501; i < test_size; ++i) {
        tree.remove(i);
        verify_tree_structure(tree);
    }

    Assert::that(tree.empty()).is_true();
}

TEST(Find) {
    rbtree<int> tree;

    for (int i = 0; i < 100; ++i)
        tree.push(i);

    Assert::that(tree.find(50)).is_not_equal(tree.end());
    Assert::that(tree.find(101)).is_equal(tree.end());
}

TEST(RemoveWithIterator) {
    rbtree<int> tree;

    tree.push(1);

    auto itr = tree.find(1);
    tree.remove(itr);

    Assert::that(tree.empty()).is_true();
}

TEST(GreaterCompare) {
    rbtree<int, kernel::Greater<int>> tree;

    for (int i = 0; i < 100; ++i)
        tree.push(i);

    for (int i = 0; i < 100; ++i)
        Assert::that(tree.contains(i)).is_true();

    Assert::that(*tree.begin()).is_equal(99);
    Assert::that(*(--tree.end())).is_equal(0);
    Assert::that(tree.m_super_root.left->value).is_equal(99);
    Assert::that(tree.m_super_root.right->value).is_equal(0);

    int expected = 99;
    for (const auto& elem : tree) {
        Assert::that(elem).is_equal(expected--);
    }

    Assert::that(expected).is_equal(-1);
}

TEST(ReturnedIterator) {
    rbtree<int> tree;

    auto res = tree.emplace(1);
    Assert::that(*res.first).is_equal(1);
    Assert::that(res.second).is_true();


    auto res1 = tree.emplace(1);
    Assert::that(res1.first).is_equal(res.first);
    Assert::that(res1.second).is_false();
}

#include "Common/UniquePtr.h"

struct HelloWorld {
    HelloWorld(int x) : x(x) {}

    int x;

    friend bool operator<(const kernel::UniquePtr<HelloWorld>& l, int r)
    {
        return l->x < r;
    }

    friend bool operator<(int l, const kernel::UniquePtr<HelloWorld>& r)
    {
        return l < r->x;
    }

    friend bool operator<(const kernel::UniquePtr<HelloWorld>& l, const kernel::UniquePtr<HelloWorld>& r)
    {
        return l->x < r->x;
    }
};

TEST(TransparentComparator) {
    rbtree<kernel::UniquePtr<HelloWorld>, kernel::Less<>> transparent_tree;

    transparent_tree.emplace(kernel::UniquePtr<HelloWorld>::create(5));
    transparent_tree.emplace(kernel::UniquePtr<HelloWorld>::create(3));
    transparent_tree.emplace(kernel::UniquePtr<HelloWorld>::create(2));
    transparent_tree.emplace(kernel::UniquePtr<HelloWorld>::create(1));
    transparent_tree.emplace(kernel::UniquePtr<HelloWorld>::create(4));

    int expected_number = 1;
    for (auto& elem : transparent_tree)
        Assert::that(elem->x).is_equal(expected_number++);

    auto number_3 = transparent_tree.find(3);
    Assert::that(number_3).is_not_equal(transparent_tree.end());
    Assert::that(number_3->get()->x).is_equal(3);
    Assert::that(transparent_tree.get(3)->x).is_equal(3);
    Assert::that(transparent_tree.contains(3)).is_true();
}

TEST(Case6ColorInheritance) {
    using kernel::Range;

    rbtree<kernel::Range> tree;

    using ValueNode = rbtree<kernel::Range>::ValueNode;
    using Color = rbtree<kernel::Range>::ValueNode::Color;

    auto* five = new ValueNode(Range::from_two_pointers(0xFFFF800100DD9000, 0xFFFF800100DE2000));
    five->color = Color::BLACK;
    tree.m_root = five;

    // left side
    auto* three = tree.m_root->left = new ValueNode(Range::from_two_pointers(0xFFFF800100D76000, 0xFFFF800100D88000));
    three->parent = tree.m_root;
    three->color = Color::RED;

    auto* one = three->left = new ValueNode(Range::from_two_pointers(0xFFFF800100000000, 0xFFFF800100D52000));
    one->parent = three;
    one->color = Color::BLACK;

    auto* two = one->right = new ValueNode(Range::from_two_pointers(0xFFFF800100D5B000, 0xFFFF800100D6D000));
    two->parent = one;
    two->color = Color::RED;

    auto* four = three->right = new ValueNode(Range::from_two_pointers(0xFFFF800100D91000, 0xFFFF800100D9A000));
    four->parent = three;
    four->color = Color::BLACK;

    // right side
    auto* seven = tree.m_root->right = new ValueNode(Range::from_two_pointers(0xFFFF800100E60000, 0xFFFF800100E69000));
    seven->color = Color::RED;
    seven->parent = tree.m_root;

    auto* six = seven->left = new ValueNode(Range::from_two_pointers(0xFFFF800100DF4000, 0xFFFF800100E57000));
    six->parent = seven;
    six->color = Color::BLACK;

    auto* nine = seven->right = new ValueNode(Range::from_two_pointers(0xFFFFFFFF80000000, 0xFFFFFFFF80800000));
    nine->parent = seven;
    nine->color = Color::BLACK;

    auto* eight = nine->left = new ValueNode(Range::from_two_pointers(0xFFFF800100ED5000, 0xFFFF800100F92000));
    eight->parent = nine;
    eight->color = Color::RED;

    tree.m_size = 9;
    tree.m_root->parent = tree.super_root_as_value_node();
    tree.m_super_root.left = one;
    tree.m_super_root.right = nine;

    tree.remove_node(six);

    Assert::that(one->color).is_equal(Color::BLACK);
    Assert::that(two->color).is_equal(Color::RED);
    Assert::that(three->color).is_equal(Color::RED);
    Assert::that(four->color).is_equal(Color::BLACK);
    Assert::that(five->color).is_equal(Color::BLACK);
    Assert::that(seven->color).is_equal(Color::BLACK);
    Assert::that(eight->color).is_equal(Color::RED);
    Assert::that(nine->color).is_equal(Color::BLACK);

    Assert::that(eight->left).is_equal(seven);
    Assert::that(eight->right).is_equal(nine);

    verify_tree_structure(tree);
}
