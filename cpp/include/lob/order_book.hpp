#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>

#include "lob/types.hpp"

namespace lob {

// A price-level (L2) book for one symbol, kept to the subscribed depth.
//
// STAGE 2 IS IMPLEMENTING THIS CLASS (src/order_book.cpp).
//   1. Make tests/test_order_book.cpp pass:   make test
//   2. Prove it on real data:                 make replay
//      (every Kraken checksum must match)
//   3. Measure it:                            make bench
//   4. Replace the std::maps with something faster, re-run 1-3, and keep a
//      table of what you tried and what it bought you. That table is the
//      interesting part of the write-up.
//
// You may change any private member. Keep the public interface: lob_replay,
// lob_bench and the tests use it.
class OrderBook {
 public:
  explicit OrderBook(std::size_t depth) : depth_(depth) {}

  // Remove every level. A Kraken snapshot replaces the whole book.
  void clear();

  // Set the level at `price` to `qty`; qty == 0 deletes the level.
  void apply(Side side, Price price, Qty qty);

  // Drop levels beyond depth() on both sides. Kraken never sends a delete for
  // a level that falls out of the subscribed depth, so the replay calls this
  // once after applying every update in a message.
  void truncate();

  // Kraken's CRC32 over the top 10 asks then the top 10 bids (README, "The
  // checksum"). The replay compares it with the checksum on every message.
  std::uint32_t checksum() const;

  std::optional<Level> best_bid() const;
  std::optional<Level> best_ask() const;

  // Copy up to out.size() levels of one side into `out`, best price first.
  // Returns how many were written.
  std::size_t top(Side side, std::span<Level> out) const;

  std::size_t size(Side side) const;
  std::size_t depth() const { return depth_; }

 private:
  std::size_t depth_;
  // Suggested first version. Correct first, fast later.
  std::map<Price, Qty, std::greater<>> bids_;  // iteration order: best (highest) first
  std::map<Price, Qty> asks_;                  // iteration order: best (lowest) first
};

}  // namespace lob
