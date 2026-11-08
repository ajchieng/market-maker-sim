#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace lob {

// Exact decimal text -> integer ticks: to_ticks("0.00006120", 8) == 6120.
// Throws std::invalid_argument on malformed input or on non-zero digits past
// `precision` (that would mean the precision table is wrong).
std::int64_t to_ticks(std::string_view text, int precision);

// Integer ticks -> decimal text: format_ticks(6120, 8) == "0.00006120".
std::string format_ticks(std::int64_t ticks, int precision);

}  // namespace lob
