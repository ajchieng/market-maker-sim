#include "lob/feed.hpp"

#include <chrono>
#include <stdexcept>

#include "lob/decimal.hpp"

namespace lob {

namespace sj = simdjson::ondemand;

namespace {

std::int64_t digits(std::string_view s, std::size_t pos, std::size_t len) {
  std::int64_t v = 0;
  for (std::size_t i = pos; i < pos + len; ++i) {
    if (i >= s.size() || s[i] < '0' || s[i] > '9') {
      throw std::invalid_argument("bad timestamp: " + std::string(s));
    }
    v = v * 10 + (s[i] - '0');
  }
  return v;
}

std::vector<LevelUpdate> parse_levels(sj::array levels, const Instrument& inst) {
  std::vector<LevelUpdate> out;
  for (sj::object level : levels) {
    LevelUpdate u;
    u.price = to_ticks(level["price"].raw_json_token().value(), inst.price_precision);
    u.qty = to_ticks(level["qty"].raw_json_token().value(), inst.qty_precision);
    out.push_back(u);
  }
  return out;
}

}  // namespace

std::int64_t parse_rfc3339_ns(std::string_view s) {
  if (s.size() < 20 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' ||
      s[16] != ':' || s.back() != 'Z') {
    throw std::invalid_argument("bad timestamp: " + std::string(s));
  }
  using namespace std::chrono;
  const auto ymd = year{static_cast<int>(digits(s, 0, 4))} /
                   month{static_cast<unsigned>(digits(s, 5, 2))} /
                   day{static_cast<unsigned>(digits(s, 8, 2))};
  if (!ymd.ok()) throw std::invalid_argument("bad date: " + std::string(s));
  const std::int64_t days = sys_days{ymd}.time_since_epoch().count();
  const std::int64_t secs =
      days * 86400 + digits(s, 11, 2) * 3600 + digits(s, 14, 2) * 60 + digits(s, 17, 2);

  std::int64_t frac_ns = 0;
  std::size_t i = 19;
  if (s[i] == '.') {
    int n = 0;
    for (++i; i < s.size() - 1; ++i, ++n) {
      if (n < 9) frac_ns = frac_ns * 10 + digits(s, i, 1);
    }
    for (; n < 9; ++n) frac_ns *= 10;
  } else if (i != s.size() - 1) {
    throw std::invalid_argument("bad timestamp: " + std::string(s));
  }
  return secs * 1'000'000'000 + frac_ns;
}

const Instrument& FeedParser::instrument(std::string_view symbol) const {
  const auto it = instruments_.find(std::string(symbol));
  if (it == instruments_.end()) {
    throw std::runtime_error("symbol not in recording header: " + std::string(symbol));
  }
  return it->second;
}

void FeedParser::parse_line(std::string_view line, std::vector<Event>& out) {
  buf_.reserve(line.size() + simdjson::SIMDJSON_PADDING);
  buf_.assign(line);
  try {
    sj::document doc = parser_.iterate(buf_.data(), buf_.size(), buf_.capacity());
    const std::int64_t recv_ns = doc["recv_ns"].get_int64();
    sj::object msg = doc["msg"].get_object();

    std::string_view channel;
    if (msg["channel"].get_string().get(channel) != simdjson::SUCCESS) return;  // acks

    if (channel == "book") {
      const bool snapshot = std::string_view(msg["type"].get_string().value()) == "snapshot";
      for (sj::object d : msg["data"].get_array()) {
        BookMsg m;
        m.symbol = std::string_view(d["symbol"].get_string().value());
        const Instrument& inst = instrument(m.symbol);
        m.is_snapshot = snapshot;
        m.recv_ns = recv_ns;
        m.bids = parse_levels(d["bids"].get_array(), inst);
        m.asks = parse_levels(d["asks"].get_array(), inst);
        m.checksum = static_cast<std::uint32_t>(d["checksum"].get_uint64().value());
        m.exch_ns = parse_rfc3339_ns(d["timestamp"].get_string().value());
        out.emplace_back(std::move(m));
      }
    } else if (channel == "trade") {
      for (sj::object d : msg["data"].get_array()) {
        Trade t;
        t.symbol = std::string_view(d["symbol"].get_string().value());
        const Instrument& inst = instrument(t.symbol);
        t.taker_side = std::string_view(d["side"].get_string().value()) == "buy" ? Side::Bid : Side::Ask;
        t.price = to_ticks(d["price"].raw_json_token().value(), inst.price_precision);
        t.qty = to_ticks(d["qty"].raw_json_token().value(), inst.qty_precision);
        t.trade_id = d["trade_id"].get_int64();
        t.exch_ns = parse_rfc3339_ns(d["timestamp"].get_string().value());
        t.recv_ns = recv_ns;
        out.emplace_back(std::move(t));
      }
    } else if (channel == "mmsim.header") {
      sj::object d = msg["data"].get_object();
      Header h;
      h.book_depth = static_cast<std::size_t>(d["book_depth"].get_uint64().value());
      for (sj::object p : d["pairs"].get_array()) {
        Instrument inst;
        inst.symbol = std::string_view(p["symbol"].get_string().value());
        inst.price_precision = static_cast<int>(p["price_precision"].get_int64().value());
        inst.qty_precision = static_cast<int>(p["qty_precision"].get_int64().value());
        instruments_[inst.symbol] = inst;
        h.instruments.push_back(inst);
      }
      out.emplace_back(std::move(h));
    }
    // status and anything else: no events
  } catch (const simdjson::simdjson_error& e) {
    throw std::runtime_error(std::string("JSON error (") + e.what() + ") in: " +
                             std::string(line.substr(0, 200)));
  } catch (const std::invalid_argument& e) {
    throw std::runtime_error(std::string(e.what()) + " in: " + std::string(line.substr(0, 200)));
  }
}

}  // namespace lob
