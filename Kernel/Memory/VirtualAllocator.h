#pragma once

#include "Common/Lock.h"
#include "Common/Logger.h"
#include "Common/Macros.h"
#include "Common/Set.h"
#include "Common/String.h"
#include "Common/Types.h"

#include "Page.h"
#include "Range.h"

namespace kernel {

class VirtualAllocator {
    MAKE_NONCOPYABLE(VirtualAllocator);

public:
    using RangeIterator = Set<Range, Less<>>::Iterator;

    VirtualAllocator() = default;
    VirtualAllocator(Address begin, Address end);

    void reset_with(Address begin, Address end);

    bool contains(Address address);
    bool contains(const Range& range);

    Range allocate(size_t length, size_t alignment = Page::size);
    Range allocate(Range);
    void deallocate(const Range& range);

    bool is_allocated(Address) const;

    String debug_dump() const;

    const Range& base_range() const { return m_base_range; }

private:
    void merge_and_emplace(RangeIterator before, RangeIterator after, Range new_range);

private:
    Range m_base_range;

    Set<Range, Less<>> m_allocated_ranges;

    mutable InterruptSafeSpinLock m_lock;
};
}
