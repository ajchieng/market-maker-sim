// Stage 2's definition of done. Every test here fails until OrderBook is
// implemented; run them with `make test`.

#include <array>
#include <map>

#include <catch2/catch_test_macros.hpp>

#include "lob/feed.hpp"
#include "lob/gz_lines.hpp"
#include "lob/order_book.hpp"

using namespace lob;

TEST_CASE("apply inserts, overwrites and deletes levels", "[book]") {
  OrderBook b(10);
  b.apply(Side::Bid, 100, 5);
  b.apply(Side::Bid, 99, 7);
  CHECK(b.size(Side::Bid) == 2);
  b.apply(Side::Bid, 100, 3);  // overwrite, not add
  CHECK(b.best_bid() == Level{100, 3});
  b.apply(Side::Bid, 100, 0);  // qty 0 deletes
  CHECK(b.best_bid() == Level{99, 7});
  b.apply(Side::Bid, 42, 0);  // deleting a level that isn't there is harmless
  CHECK(b.size(Side::Bid) == 1);
}

TEST_CASE("best bid is the highest price, best ask the lowest", "[book]") {
  OrderBook b(10);
  CHECK_FALSE(b.best_bid().has_value());
  CHECK_FALSE(b.best_ask().has_value());
  for (Price p : {101, 105, 103}) b.apply(Side::Ask, p, 1);
  for (Price p : {97, 95, 99}) b.apply(Side::Bid, p, 1);
  CHECK(b.best_ask()->price == 101);
  CHECK(b.best_bid()->price == 99);
}

TEST_CASE("top copies levels best first", "[book]") {
  OrderBook b(10);
  for (Price p : {101, 105, 103}) b.apply(Side::Ask, p, p - 100);
  std::array<Level, 5> out{};
  REQUIRE(b.top(Side::Ask, out) == 3);
  CHECK(out[0] == Level{101, 1});
  CHECK(out[1] == Level{103, 3});
  CHECK(out[2] == Level{105, 5});
  std::array<Level, 2> two{};
  CHECK(b.top(Side::Ask, two) == 2);
}

TEST_CASE("truncate keeps only the best `depth` levels per side", "[book]") {
  OrderBook b(3);
  for (Price p = 1; p <= 6; ++p) {
    b.apply(Side::Bid, p, 1);
    b.apply(Side::Ask, 100 + p, 1);
  }
  b.truncate();
  CHECK(b.size(Side::Bid) == 3);
  CHECK(b.size(Side::Ask) == 3);
  std::array<Level, 3> bids{}, asks{};
  b.top(Side::Bid, bids);
  b.top(Side::Ask, asks);
  CHECK(bids[2].price == 4);  // 6, 5, 4 survive
  CHECK(asks[2].price == 103);  // 101, 102, 103 survive
}

TEST_CASE("clear empties both sides", "[book]") {
  OrderBook b(10);
  b.apply(Side::Bid, 1, 1);
  b.apply(Side::Ask, 2, 1);
  b.clear();
  CHECK(b.size(Side::Bid) == 0);
  CHECK(b.size(Side::Ask) == 0);
}

TEST_CASE("checksum: asks then bids, price then qty, no dots or leading zeros", "[book]") {
  // BTC/USD (price_precision 1, qty_precision 8). Kraken hashes the string
  //   "757672" "63495943" "757674" "24383666"   asks, lowest first
  //   "757671" "16757109" "757648" "6120"       bids, highest first
  OrderBook b(10);
  b.apply(Side::Bid, 757671, 16757109);  // 75767.1 x 0.16757109
  b.apply(Side::Bid, 757648, 6120);      // 75764.8 x 0.00006120
  b.apply(Side::Ask, 757674, 24383666);  // 75767.4 x 0.24383666
  b.apply(Side::Ask, 757672, 63495943);  // 75767.2 x 0.63495943
  CHECK(b.checksum() == 2540394140u);
}

TEST_CASE("checksum only covers the top 10 levels", "[book]") {
  OrderBook a(25), b(25);
  for (Price p = 1; p <= 10; ++p) {
    a.apply(Side::Bid, 1000 - p, 1);
    b.apply(Side::Bid, 1000 - p, 1);
    a.apply(Side::Ask, 1000 + p, 1);
    b.apply(Side::Ask, 1000 + p, 1);
  }
  b.apply(Side::Bid, 500, 9);   // 11th bid
  b.apply(Side::Ask, 1500, 9);  // 11th ask
  CHECK(a.checksum() == b.checksum());
}

TEST_CASE("replaying a real Kraken recording matches every checksum", "[book][kraken]") {
  // tests/fixtures/kraken_sample.jsonl.gz: the first 2,000 lines of a live
  // recording of BTC/USD, ETH/USD and DOGE/USD (depth 10), ~2,000 book messages.
  GzLineReader reader(LOB_FIXTURE);
  FeedParser parser;
  std::vector<Event> events;
  std::map<std::string, OrderBook> books;
  std::size_t depth = 0, checked = 0, failed = 0;
  std::string line;
  while (reader.next(line)) {
    events.clear();
    parser.parse_line(line, events);
    for (const Event& ev : events) {
      if (const auto* h = std::get_if<Header>(&ev)) depth = h->book_depth;
      const auto* m = std::get_if<BookMsg>(&ev);
      if (m == nullptr) continue;
      OrderBook& book = books.try_emplace(m->symbol, depth).first->second;
      if (m->is_snapshot) book.clear();
      for (const auto& u : m->bids) book.apply(Side::Bid, u.price, u.qty);
      for (const auto& u : m->asks) book.apply(Side::Ask, u.price, u.qty);
      book.truncate();
      ++checked;
      if (book.checksum() != m->checksum) {
        if (failed++ == 0) {
          UNSCOPED_INFO("first mismatch: " << m->symbol << " " << (m->is_snapshot ? "snapshot" : "update")
                                           << " #" << checked);
        }
      }
    }
  }
  CHECK(checked > 1900);
  CHECK(failed == 0);
}
