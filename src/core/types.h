#pragma once

#include <cstdint>
#include <climits>

namespace vp {

using TimeMs = uint64_t;
constexpr TimeMs kInvalidTime = UINT64_MAX;

struct TimeRange {
    TimeMs start = 0;
    TimeMs duration = 0;

    TimeMs end() const { return start + duration; }
};

} // namespace vp
