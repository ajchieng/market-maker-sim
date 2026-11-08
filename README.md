# market-maker-sim

Record Kraken's live crypto order book, rebuild it in C++ and verify it against
the exchange's own checksums, then backtest a market-making strategy with a
pessimistic fill model and find out where the P&L really comes from.

```
Kraken WS v2 ─► mm-record ─► data/raw/*.jsonl.gz ─► lob_replay ─► data/events/*.csv ─► backtest ─► analysis
                (Python)      raw messages,          (C++) book +    top-5 book rows,     strategy +   markouts,
                              exact digits           CRC32 check     trades               fill model   P&L split
```

## Status

| Stage | Where | State |
|---|---|---|
| 1. Recorder | `src/mmsim/record.py` | Done. Tested on live data. |
| 2. Order book in C++ | `cpp/src/order_book.cpp` | **Yours.** 8 failing tests define "done". Parsing, replay and benchmark harness are provided. |
| 3. Strategy, fills, backtest | `src/mmsim/strategy.py`, `fills.py` | **Yours.** Event loop, accounting and a baseline strategy are provided. |
| 4. Analysis | `src/mmsim/analysis.py` | **Yours.** |
| Stretch: ITCH parser in SystemVerilog | `hdl/` | Plan only. |

## Quick start

Needs `uv`, CMake ≥ 3.24 and a C++20 compiler.

```bash
make setup              # Python deps + C++ configure (downloads simdjson, Catch2)
make record MINUTES=30  # live BTC/USD + ETH/USD into data/raw/
make test               # Python tests pass; the C++ [book] tests fail until stage 2
```

After stage 2:

```bash
make replay             # newest recording -> data/events/, checks every checksum
make bench              # ns per level update, per-message p50/p99
```

To record for hours, run `make record MINUTES=600` under `caffeinate -i` (the
Mac must not sleep). Recordings are about 4 MB/hour gzipped for three symbols
at depth 10. Kraken posts maintenance windows on the `status` channel; the
recorder reconnects and starts a new file.

## Stage 2: the order book

Implement `OrderBook` (`cpp/include/lob/order_book.hpp`). The rules below were
checked against 10,101 live book messages on 8 Nov 2026:

- **Integer ticks.** The parser hands you prices and quantities as `int64`
  counts of the smallest unit (`75767.1` → `757671` for BTC/USD). No floats.
- **`qty == 0` deletes** the level.
- **Apply levels in the order sent.** One message can touch the same price twice.
- **Snapshot = replace.** Clear the book first.
- **Truncate after each message.** When a level drops out of your subscribed
  depth, Kraken sends no delete. Trim each side to `depth` yourself.
- **The checksum.** Take up to 10 asks (lowest first), then up to 10 bids
  (highest first). For each level append the price digits, then the qty
  digits, with no `.` and no leading zeros. CRC32 (zlib's `crc32`) of that
  string must equal the message's `checksum`. In integer ticks this is just
  `to_string(price) + to_string(qty)`.

Done means `./build/lob_tests` passes and `make replay` reports 0 checksum
failures on a long recording.

**Then make it fast.** Start with the `std::map` members provided, record the
`make bench` numbers, and iterate. Ideas: the book only holds 10 levels a side,
so a sorted array may beat a tree; the checksum string allocation probably
costs more than the book update; `std::to_chars` into a stack buffer; feeding
`crc32` level by level instead of building a string. Keep a table of each
change with ns/update, with and without `--no-checksum`. Build in Release (the
default) before quoting numbers.

## Stage 3: strategy and fills

1. Read `fills.py` and implement `ConservativeFillModel`. Its six rules
   (latency, queue position, trades through your price, cancels, ...) are
   where backtests usually lie.
2. Run the provided `JoinTouch` baseline through `backtest.run` (example in
   `backtest.py`'s docstring).
3. Implement `AvellanedaStoikov` in `strategy.py`. Estimate `sigma` and `k`
   from your own data first.
4. Run everything at `maker_fee_bps=0` (is there any edge?) and at your real
   Kraken fee tier. Retail maker fees are many times BTC/USD's spread, so
   expect a loss after fees and explain why.

## Stage 4: analysis

Implement `markouts` and `pnl_decomposition` in `analysis.py` (formulas in the
docstrings). The write-up should answer:

- Mean markout at 1 s, 10 s and 60 s for buys and sells. Are you being picked off?
- How P&L splits into spread capture, adverse selection, inventory and fees.
- How much the result moves when you halve the latency or change the queue
  assumption. If it moves a lot, the edge comes from the fill model.

## Data notes

- Drive everything off `recv_ns` (when you knew). `exch_ns` is Kraken's clock
  and differs from yours by milliseconds; in the first recording some trades
  arrived "before" Kraken timestamped them.
- Each file starts with a header line (`mmsim.header`) holding the depth and
  per-symbol precisions. After a disconnect or rotation, a new file starts
  with a fresh snapshot.
- `cpp/tests/fixtures/kraken_sample.jsonl.gz` is the first 2,000 lines of a
  real recording and is what the tests replay.

## Layout

```
src/mmsim/      record.py (stage 1), events.py, strategy.py, fills.py, backtest.py, analysis.py
cpp/include/    lob/types.hpp, order_book.hpp, feed.hpp, decimal.hpp, gz_lines.hpp
cpp/src/        order_book.cpp (stage 2), feed.cpp, decimal.cpp, gz_lines.cpp, replay_main.cpp, bench_main.cpp
cpp/tests/      test_feed.cpp (passing), test_order_book.cpp (stage 2), fixtures/
tests/          Python tests
hdl/            stretch goal plan
```

## Résumé bullets (fill in the blanks when they are true)

- Built a C++20 L2 order book for Kraken's WebSocket feed, verified against
  exchange CRC32 checksums on __ million messages; __ ns per update (__× faster
  than a `std::map` baseline).
- Backtested an Avellaneda–Stoikov market maker with a queue-aware fill model;
  split P&L into spread capture (+__) and adverse selection (−__) using markouts.
