#include "lob/order_book.hpp"

#include <stdexcept>
#include <string>

#include <zlib.h>

namespace lob {

namespace {

[[noreturn]] void todo(const char* method) {
  throw std::logic_error(std::string("TODO (stage 2): implement OrderBook::") + method +
                         " in cpp/src/order_book.cpp");
}

}  // namespace

void OrderBook::clear() { todo("clear"); }

void OrderBook::apply(Side side, Price price, Qty qty) {
  // TODO: insert, overwrite or erase the level at `price` on `side`.
  (void)side, (void)price, (void)qty;
  todo("apply");
}

void OrderBook::truncate() {
  // TODO: erase every level past depth_ on each side.
  todo("truncate");
}

std::uint32_t OrderBook::checksum() const {
  // TODO: build the checksum string (README, "The checksum"): up to 10 asks,
  // best first, then up to 10 bids, best first; each level contributes
  // std::to_string(price) + std::to_string(qty). Integer ticks already have no
  // '.' and no leading zeros, which is exactly what Kraken wants. Then:
  //
  //   return static_cast<std::uint32_t>(
  //       crc32(0L, reinterpret_cast<const Bytef*>(s.data()), static_cast<uInt>(s.size())));
  todo("checksum");
}

std::optional<Level> OrderBook::best_bid() const { todo("best_bid"); }

std::optional<Level> OrderBook::best_ask() const { todo("best_ask"); }

std::size_t OrderBook::top(Side side, std::span<Level> out) const {
  (void)side, (void)out;
  todo("top");
}

std::size_t OrderBook::size(Side side) const {
  (void)side;
  todo("size");
}

}  // namespace lob
