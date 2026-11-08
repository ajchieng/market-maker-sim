#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include <simdjson.h>

#include "lob/types.hpp"

namespace lob {

// Turns lines written by `mm-record` (src/mmsim/record.py) into typed events.
// Prices and quantities are converted to integer ticks straight from the JSON
// text, never through a double.

struct Instrument {
  std::string symbol;
  int price_precision = 0;  // decimals in a price: 1 for BTC/USD (tick 0.1)
  int qty_precision = 0;    // decimals in a quantity: 8 for BTC/USD
};

// Line 1 of every recording.
struct Header {
  std::size_t book_depth = 0;
  std::vector<Instrument> instruments;
};

struct LevelUpdate {
  Price price = 0;
  Qty qty = 0;  // 0 means delete the level
};

// One symbol's part of a Kraken `book` message. Apply bids and asks in the
// order given: one message can touch the same level twice.
struct BookMsg {
  std::string symbol;
  bool is_snapshot = false;
  std::vector<LevelUpdate> bids;
  std::vector<LevelUpdate> asks;
  std::uint32_t checksum = 0;
  std::int64_t exch_ns = 0;  // Kraken's timestamp
  std::int64_t recv_ns = 0;  // when the recorder received it
};

struct Trade {
  std::string symbol;
  Side taker_side = Side::Bid;  // Bid = an aggressive buy lifted the ask
  Price price = 0;
  Qty qty = 0;
  std::int64_t trade_id = 0;
  std::int64_t exch_ns = 0;
  std::int64_t recv_ns = 0;
};

using Event = std::variant<Header, BookMsg, Trade>;

class FeedParser {
 public:
  // Parse one recorded line and append its events to `out`. Acks and status
  // messages produce no events. Throws std::runtime_error on malformed input.
  void parse_line(std::string_view line, std::vector<Event>& out);

  // Precisions for a symbol named in the header. Throws if unknown.
  const Instrument& instrument(std::string_view symbol) const;

 private:
  simdjson::ondemand::parser parser_;
  std::string buf_;  // line copy with simdjson's required padding
  std::unordered_map<std::string, Instrument> instruments_;
};

// "2026-11-08T19:23:43.287125Z" -> nanoseconds since the Unix epoch.
std::int64_t parse_rfc3339_ns(std::string_view text);

}  // namespace lob
