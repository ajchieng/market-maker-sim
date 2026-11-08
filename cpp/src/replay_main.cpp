// lob_replay: rebuild the book from a recording, check every Kraken checksum,
// and write the event files the Python backtester reads.
//
//   lob_replay data/raw/kraken_<stamp>.jsonl.gz [--out data/events] [--levels 5]
//
// For each symbol it writes
//   <out>/<stamp>.<SYMBOL>.book.csv    one row whenever the top --levels change
//   <out>/<stamp>.<SYMBOL>.trades.csv  one row per trade
// and exits non-zero if any checksum failed.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "lob/decimal.hpp"
#include "lob/feed.hpp"
#include "lob/gz_lines.hpp"
#include "lob/order_book.hpp"

namespace fs = std::filesystem;
using namespace lob;

namespace {

template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};

struct SymbolState {
  Instrument inst;
  OrderBook book;
  std::ofstream book_csv;
  std::ofstream trades_csv;
  std::vector<Level> bids, asks, prev_bids, prev_asks;
  std::size_t bid_n = 0, ask_n = 0;
  bool synced = false;  // seen a snapshot yet
  std::uint64_t book_msgs = 0, bad_checksums = 0, trades = 0;

  SymbolState(Instrument i, std::size_t depth, std::size_t levels)
      : inst(std::move(i)), book(depth), bids(levels), asks(levels), prev_bids(levels), prev_asks(levels) {}
};

std::string file_safe(std::string s) {
  for (char& c : s) {
    if (c == '/') c = '-';
  }
  return s;
}

void print_side(const char* name, const OrderBook& book, Side side, const Instrument& inst) {
  std::vector<Level> lv(5);
  const std::size_t n = book.top(side, lv);
  std::cerr << "    " << name << ":";
  for (std::size_t i = 0; i < n; ++i) {
    std::cerr << "  " << format_ticks(lv[i].price, inst.price_precision) << " x "
              << format_ticks(lv[i].qty, inst.qty_precision);
  }
  std::cerr << "\n";
}

int usage() {
  std::cerr << "usage: lob_replay <recording.jsonl.gz> [--out DIR] [--levels N]\n";
  return 2;
}

}  // namespace

int run(int argc, char** argv) {
  if (argc < 2) return usage();
  const fs::path input = argv[1];
  fs::path out_dir = "data/events";
  std::size_t levels = 5;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--out" && i + 1 < argc) {
      out_dir = argv[++i];
    } else if (a == "--levels" && i + 1 < argc) {
      levels = std::stoul(argv[++i]);
    } else {
      return usage();
    }
  }
  fs::create_directories(out_dir);
  std::string stem = input.filename().string();
  stem = stem.substr(0, stem.find(".jsonl"));

  GzLineReader reader(input.string());
  FeedParser parser;
  std::map<std::string, std::unique_ptr<SymbolState>> symbols;
  std::vector<Event> events;
  std::string line;
  std::uint64_t line_no = 0;

  while (reader.next(line)) {
    ++line_no;
    events.clear();
    parser.parse_line(line, events);
    for (const Event& ev : events) {
      std::visit(
          overloaded{
              [&](const Header& h) {
                for (const Instrument& inst : h.instruments) {
                  auto s = std::make_unique<SymbolState>(inst, h.book_depth, levels);
                  const std::string base = (out_dir / (stem + "." + file_safe(inst.symbol))).string();
                  s->book_csv.open(base + ".book.csv");
                  s->trades_csv.open(base + ".trades.csv");
                  s->book_csv << "recv_ns,exch_ns";
                  for (std::size_t i = 0; i < levels; ++i) s->book_csv << ",bid_px_" << i << ",bid_qty_" << i;
                  for (std::size_t i = 0; i < levels; ++i) s->book_csv << ",ask_px_" << i << ",ask_qty_" << i;
                  s->book_csv << "\n";
                  s->trades_csv << "recv_ns,exch_ns,trade_id,taker_side,price,qty\n";
                  symbols[inst.symbol] = std::move(s);
                }
              },
              [&](const BookMsg& m) {
                SymbolState& s = *symbols.at(m.symbol);
                if (m.is_snapshot) {
                  s.book.clear();
                  s.synced = true;
                }
                if (!s.synced) return;
                for (const LevelUpdate& u : m.bids) s.book.apply(Side::Bid, u.price, u.qty);
                for (const LevelUpdate& u : m.asks) s.book.apply(Side::Ask, u.price, u.qty);
                s.book.truncate();
                ++s.book_msgs;

                if (s.book.checksum() != m.checksum && s.bad_checksums++ == 0) {
                  std::cerr << m.symbol << ": first checksum mismatch at line " << line_no << " (got "
                            << s.book.checksum() << ", Kraken sent " << m.checksum << ")\n";
                  print_side("asks", s.book, Side::Ask, s.inst);
                  print_side("bids", s.book, Side::Bid, s.inst);
                }

                const std::size_t nb = s.book.top(Side::Bid, s.bids);
                const std::size_t na = s.book.top(Side::Ask, s.asks);
                const bool unchanged = nb == s.bid_n && na == s.ask_n &&
                                       std::equal(s.bids.begin(), s.bids.begin() + nb, s.prev_bids.begin()) &&
                                       std::equal(s.asks.begin(), s.asks.begin() + na, s.prev_asks.begin());
                if (unchanged) return;
                s.bid_n = nb;
                s.ask_n = na;
                s.prev_bids = s.bids;
                s.prev_asks = s.asks;

                auto& f = s.book_csv;
                f << m.recv_ns << ',' << m.exch_ns;
                auto write_side = [&](const std::vector<Level>& lv, std::size_t n) {
                  for (std::size_t i = 0; i < levels; ++i) {
                    if (i < n) {
                      f << ',' << format_ticks(lv[i].price, s.inst.price_precision) << ','
                        << format_ticks(lv[i].qty, s.inst.qty_precision);
                    } else {
                      f << ",,";
                    }
                  }
                };
                write_side(s.bids, nb);
                write_side(s.asks, na);
                f << '\n';
              },
              [&](const Trade& t) {
                SymbolState& s = *symbols.at(t.symbol);
                ++s.trades;
                s.trades_csv << t.recv_ns << ',' << t.exch_ns << ',' << t.trade_id << ','
                             << (t.taker_side == Side::Bid ? "buy" : "sell") << ','
                             << format_ticks(t.price, s.inst.price_precision) << ','
                             << format_ticks(t.qty, s.inst.qty_precision) << '\n';
              },
          },
          ev);
    }
  }

  if (reader.truncated()) {
    std::cerr << "note: " << input << " ends mid-stream (recorder was killed); replayed what was there\n";
  }
  int status = 0;
  for (const auto& [name, s] : symbols) {
    const double ok = s->book_msgs ? 100.0 * double(s->book_msgs - s->bad_checksums) / double(s->book_msgs) : 0.0;
    std::printf("%-10s %9llu book msgs  %9llu checksum failures  (%.3f%% ok)  %8llu trades\n", name.c_str(),
                static_cast<unsigned long long>(s->book_msgs), static_cast<unsigned long long>(s->bad_checksums), ok,
                static_cast<unsigned long long>(s->trades));
    if (s->bad_checksums > 0 || s->book_msgs == 0) status = 1;
  }
  std::printf("events written to %s\n", out_dir.string().c_str());
  return status;
}

int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
