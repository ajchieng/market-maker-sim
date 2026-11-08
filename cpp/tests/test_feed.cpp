// Parsing plumbing. These pass out of the box; if one breaks, the recorder's
// output format or Kraken's message format changed.

#include <catch2/catch_test_macros.hpp>

#include "lob/decimal.hpp"
#include "lob/feed.hpp"
#include "lob/gz_lines.hpp"

using namespace lob;

TEST_CASE("to_ticks converts Kraken decimals exactly", "[feed]") {
  CHECK(to_ticks("75767.1", 1) == 757671);
  CHECK(to_ticks("75761.0", 1) == 757610);
  CHECK(to_ticks("0.00006120", 8) == 6120);
  CHECK(to_ticks("0.00000000", 8) == 0);
  CHECK(to_ticks("12", 2) == 1200);
  CHECK(to_ticks("0.1823400", 7) == 1823400);
  CHECK(to_ticks("5e-05", 8) == 5000);
  CHECK_THROWS(to_ticks("1.23", 1));  // more decimals than the instrument allows
  CHECK_THROWS(to_ticks("abc", 2));
  CHECK_THROWS(to_ticks("", 2));
}

TEST_CASE("format_ticks is the inverse of to_ticks", "[feed]") {
  CHECK(format_ticks(757671, 1) == "75767.1");
  CHECK(format_ticks(6120, 8) == "0.00006120");
  CHECK(format_ticks(0, 8) == "0.00000000");
  CHECK(format_ticks(42, 0) == "42");
  CHECK(format_ticks(-15, 1) == "-1.5");
}

TEST_CASE("parse_rfc3339_ns", "[feed]") {
  CHECK(parse_rfc3339_ns("1970-01-01T00:00:00Z") == 0);
  CHECK(parse_rfc3339_ns("1970-01-01T00:00:01.5Z") == 1'500'000'000);
  CHECK(parse_rfc3339_ns("2026-11-08T19:23:43.287125Z") == 1'794'165'823'287'125'000);
  CHECK_THROWS(parse_rfc3339_ns("2026-11-08 19:23:43Z"));
}

TEST_CASE("FeedParser reads header, book and trade lines", "[feed]") {
  FeedParser p;
  std::vector<Event> ev;

  p.parse_line(
      R"({"recv_ns":1,"msg":{"channel":"mmsim.header","data":{"source":"x","book_depth":10,"symbols":["BTC/USD"],)"
      R"("pairs":[{"symbol":"BTC/USD","price_precision":1,"qty_precision":8}]}}})",
      ev);
  REQUIRE(ev.size() == 1);
  const auto& h = std::get<Header>(ev[0]);
  CHECK(h.book_depth == 10);
  CHECK(p.instrument("BTC/USD").qty_precision == 8);

  ev.clear();
  p.parse_line(R"({"recv_ns":2,"msg":{"method":"subscribe","req_id":3,"success":true}})", ev);
  CHECK(ev.empty());  // acks produce nothing

  p.parse_line(
      R"({"recv_ns":3,"msg":{"channel":"book","type":"update","data":[{"symbol":"BTC/USD","bids":[],)"
      R"("asks":[{"price":75767.4,"qty":0.00000000},{"price":75777.1,"qty":0.13520163}],)"
      R"("checksum":4115330489,"timestamp":"2026-11-08T19:23:43.287125Z"}]}})",
      ev);
  REQUIRE(ev.size() == 1);
  const auto& b = std::get<BookMsg>(ev[0]);
  CHECK_FALSE(b.is_snapshot);
  CHECK(b.bids.empty());
  REQUIRE(b.asks.size() == 2);
  CHECK(b.asks[0].price == 757674);
  CHECK(b.asks[0].qty == 0);
  CHECK(b.asks[1].qty == 13520163);
  CHECK(b.checksum == 4115330489u);
  CHECK(b.recv_ns == 3);
  CHECK(b.exch_ns == 1'794'165'823'287'125'000);

  ev.clear();
  p.parse_line(
      R"({"recv_ns":4,"msg":{"channel":"trade","type":"update","data":[{"symbol":"BTC/USD","side":"sell",)"
      R"("price":75767.1,"qty":0.05751304,"ord_type":"limit","trade_id":114782870,)"
      R"("timestamp":"2026-11-08T19:23:48.538807Z"}]}})",
      ev);
  REQUIRE(ev.size() == 1);
  const auto& t = std::get<Trade>(ev[0]);
  CHECK(t.taker_side == Side::Ask);
  CHECK(t.price == 757671);
  CHECK(t.qty == 5751304);
  CHECK(t.trade_id == 114782870);

  CHECK_THROWS(p.parse_line(R"({"recv_ns":5,"msg":{"channel":"book","type":"update","data":[{"symbol":"ETH/USD"}]}})", ev));
}

TEST_CASE("the fixture recording parses end to end", "[feed]") {
  GzLineReader reader(LOB_FIXTURE);
  FeedParser p;
  std::vector<Event> ev;
  std::string line;
  std::size_t lines = 0;
  while (reader.next(line)) {
    p.parse_line(line, ev);
    ++lines;
  }
  CHECK_FALSE(reader.truncated());
  CHECK(lines == 2000);
  REQUIRE(!ev.empty());
  CHECK(std::holds_alternative<Header>(ev.front()));
}
