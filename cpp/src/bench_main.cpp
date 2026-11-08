// lob_bench: how fast is OrderBook on real Kraken traffic?
//
//   lob_bench data/raw/kraken_<stamp>.jsonl.gz [--symbol BTC/USD] [--reps 20] [--no-checksum]
//
// Parses the recording once, then replays one symbol's level updates through
// a fresh OrderBook --reps times. Reports throughput (ns per level update) and
// per-message latency percentiles. Build in Release (the default) before
// quoting any number.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "lob/feed.hpp"
#include "lob/gz_lines.hpp"
#include "lob/order_book.hpp"

using namespace lob;
using Clock = std::chrono::steady_clock;

namespace {

struct Msg {
  bool snapshot;
  std::uint32_t checksum;
  std::size_t first, n_bids, n_asks;  // slice of `updates`
};

int usage() {
  std::cerr << "usage: lob_bench <recording.jsonl.gz> [--symbol SYM] [--reps N] [--no-checksum]\n";
  return 2;
}

}  // namespace

int run(int argc, char** argv) {
  if (argc < 2) return usage();
  std::string symbol;
  int reps = 20;
  bool with_checksum = true;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--symbol" && i + 1 < argc) {
      symbol = argv[++i];
    } else if (a == "--reps" && i + 1 < argc) {
      reps = std::stoi(argv[++i]);
    } else if (a == "--no-checksum") {
      with_checksum = false;
    } else {
      return usage();
    }
  }

  // Parse once, outside the timed region.
  GzLineReader reader(argv[1]);
  FeedParser parser;
  std::vector<Event> events;
  std::vector<Msg> msgs;
  std::vector<LevelUpdate> updates;
  std::size_t depth = 0;
  std::string line;
  while (reader.next(line)) {
    events.clear();
    parser.parse_line(line, events);
    for (const Event& ev : events) {
      if (const auto* h = std::get_if<Header>(&ev)) {
        depth = h->book_depth;
        if (symbol.empty()) symbol = h->instruments.at(0).symbol;
      } else if (const auto* m = std::get_if<BookMsg>(&ev); m && m->symbol == symbol) {
        if (msgs.empty() && !m->is_snapshot) continue;  // wait for the first snapshot
        msgs.push_back({m->is_snapshot, m->checksum, updates.size(), m->bids.size(), m->asks.size()});
        updates.insert(updates.end(), m->bids.begin(), m->bids.end());
        updates.insert(updates.end(), m->asks.begin(), m->asks.end());
      }
    }
  }
  if (msgs.empty()) {
    std::cerr << "no book messages for " << symbol << "\n";
    return 1;
  }

  std::uint64_t sink = 0;  // keeps the optimizer from deleting the work
  auto run_msg = [&](OrderBook& book, const Msg& m) {
    if (m.snapshot) book.clear();
    const LevelUpdate* u = updates.data() + m.first;
    for (std::size_t i = 0; i < m.n_bids; ++i, ++u) book.apply(Side::Bid, u->price, u->qty);
    for (std::size_t i = 0; i < m.n_asks; ++i, ++u) book.apply(Side::Ask, u->price, u->qty);
    book.truncate();
    if (with_checksum) sink += book.checksum();
  };

  // Throughput: whole replay per rep.
  std::vector<double> ns_per_update;
  for (int r = 0; r < reps; ++r) {
    OrderBook book(depth);
    const auto t0 = Clock::now();
    for (const Msg& m : msgs) run_msg(book, m);
    const auto t1 = Clock::now();
    ns_per_update.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / double(updates.size()));
    if (auto b = book.best_bid()) sink += static_cast<std::uint64_t>(b->price);
  }
  std::sort(ns_per_update.begin(), ns_per_update.end());

  // Latency: time each message separately (adds ~20 ns of clock overhead each).
  std::vector<double> lat;
  lat.reserve(msgs.size());
  {
    OrderBook book(depth);
    for (const Msg& m : msgs) {
      const auto t0 = Clock::now();
      run_msg(book, m);
      lat.push_back(std::chrono::duration<double, std::nano>(Clock::now() - t0).count());
    }
  }
  std::sort(lat.begin(), lat.end());
  auto pct = [&](double p) { return lat[std::min(lat.size() - 1, static_cast<std::size_t>(p * double(lat.size())))]; };

  std::printf("%s: %zu messages, %zu level updates, depth %zu, checksum %s\n", symbol.c_str(), msgs.size(),
              updates.size(), depth, with_checksum ? "on" : "off");
  std::printf("throughput  best %.1f ns/update   median %.1f ns/update   (%d reps)\n", ns_per_update.front(),
              ns_per_update[ns_per_update.size() / 2], reps);
  std::printf("per message p50 %.0f ns   p99 %.0f ns   p99.9 %.0f ns   max %.0f ns\n", pct(0.50), pct(0.99), pct(0.999),
              lat.back());
  std::fprintf(stderr, "(sink %llu)\n", static_cast<unsigned long long>(sink));
  return 0;
}

int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
