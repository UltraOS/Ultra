#pragma once

#include <common/rb_tree.h>
#include <common/bit.h>

enum rb_color : u8 {
    RB_COLOR_BLACK = 0,
    RB_COLOR_RED,
};

/*
 * The color is stored in the low bits of 'parent_meta' that are guaranteed to
 * be zero because every 'struct rb_node' is aligned to at least alignof(node).
 * The mask must cover exactly those bits so that rb_node_parent() (which clears
 * them via ALIGN_DOWN) and the color accessors stay consistent.
 */
#define PARENT_META_COLOR ((ptr_t)(alignof(struct rb_node) - 1))

#define rb_node_color(node) \
    BIT_FIELD_READ((node)->parent_meta, PARENT_META_COLOR)

#define rb_node_is_red(node) (rb_node_color(node) == RB_COLOR_RED)
#define rb_node_is_black(node) (rb_node_color(node) == RB_COLOR_BLACK)

#define rb_node_set_color(node, color) \
    BIT_FIELD_WRITE((node)->parent_meta, PARENT_META_COLOR, color)

/*
 * Re-point 'node' at 'parent' while preserving its current color, which lives
 * in the low bits of 'parent_meta'.
 */
static inline void rb_set_parent(struct rb_node *node, struct rb_node *parent)
{
    node->parent_meta = (ptr_t)parent | rb_node_color(node);
}

/*
 * Re-point 'node' at 'parent' and overwrite its color in a single store.
 */
static inline void rb_set_parent_and_color(
    struct rb_node *node, struct rb_node *parent, enum rb_color color
)
{
    node->parent_meta = (ptr_t)parent | color;
}

/*
 * Replace the link from 'old's parent (or the tree root) so that it points
 * at 'new' instead. Only the link is moved here, 'new's children and color
 * are fixed up by the caller.
 */
static inline void rb_change_child(
    struct rb_node *old, struct rb_node *new,
    struct rb_node *parent, struct rb_root *root
)
{
    if (parent == nullptr)
        root->root = new;
    else if (parent->left == old)
        parent->left = new;
    else
        parent->right = new;
}
