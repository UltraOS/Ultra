#pragma once

static inline struct pt4 *pt4_from_pt5(struct pt5 *pt5, virt_addr_t addr)
{
    struct pt4 *pt4;

    if (pt5_is_folded())
        return (struct pt4*)pt5;

    pt4 = pt5_to_virt(pt5);
    return &pt4[pt4_index(addr)];
}
