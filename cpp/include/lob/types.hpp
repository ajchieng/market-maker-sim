#pragma once

#include <cstdint>

namespace lob {

// Prices and quantities are integer counts of the instrument's smallest unit.
// BTC/USD has price_precision 1 and qty_precision 8, so price 75767.1 is
// 757671 and qty 0.00006120 is 6120. Integers compare exactly (no float
// keys in a map), and Kraken's checksum becomes a plain integer-to-string.
using Price = std::int64_t;
using Qty = std::int64_t;

enum class Side : std::uint8_t { Bid, Ask };

struct Level {
  Price price = 0;
  Qty qty = 0;
  friend bool operator==(const Level&, const Level&) = default;
};

}  // namespace lob
