"""Stage 1: record Kraken WebSocket v2 market data to gzipped JSONL.

Every line is ``{"recv_ns": <local receive time, ns since epoch>, "msg": <message>}``.

* The first line of each file is a header written by this recorder
  (``"channel": "mmsim.header"``). It holds the subscribed book depth and the
  price/qty precision of each symbol, which the C++ replay needs to turn
  prices into integer ticks.
* Every later line embeds Kraken's raw message text byte for byte, so prices
  and quantities keep the exact decimal digits Kraken sent
  (``"qty":0.00006120``). Never round-trip them through a float.
* Heartbeats are dropped; everything else (acks, status, book, trade) is kept.
* One file per connection. After a reconnect the book has to be rebuilt from a
  fresh snapshot anyway, so a new file keeps every file self-contained. Files
  also rotate every ``--rotate-minutes`` (a ~1 s gap at each rotation).

Usage::

    uv run mm-record --symbols BTC/USD ETH/USD --duration 600
"""

from __future__ import annotations

import argparse
import asyncio
import gzip
import json
import logging
import signal
import time
from datetime import datetime, timezone
from pathlib import Path

from websockets.asyncio.client import ClientConnection, connect
from websockets.exceptions import ConnectionClosed, WebSocketException

URL = "wss://ws.kraken.com/v2"
BOOK_DEPTHS = (10, 25, 100, 500, 1000)
FLUSH_EVERY_S = 5.0
STATS_EVERY_S = 30.0

log = logging.getLogger("mmsim.record")


def wrap(recv_ns: int, raw: str) -> str:
    """One output line. ``raw`` is already valid JSON, so it is embedded verbatim."""
    return f'{{"recv_ns":{recv_ns},"msg":{raw}}}\n'


def header(instrument_snapshot: dict, symbols: list[str], depth: int) -> dict:
    """Build the file header from Kraken's ~1,500-pair instrument snapshot."""
    pairs = [p for p in instrument_snapshot["data"]["pairs"] if p["symbol"] in symbols]
    missing = set(symbols) - {p["symbol"] for p in pairs}
    if missing:
        raise ValueError(f"Kraken has no pair named {sorted(missing)}")
    return {
        "channel": "mmsim.header",
        "data": {
            "source": URL,
            "book_depth": depth,
            "symbols": symbols,
            "pairs": [
                {
                    "symbol": p["symbol"],
                    "price_precision": p["price_precision"],
                    "qty_precision": p["qty_precision"],
                    "price_increment": p["price_increment"],
                    "qty_increment": p["qty_increment"],
                    "qty_min": p["qty_min"],
                }
                for p in pairs
            ],
        },
    }


async def _request(ws: ClientConnection, req_id: int, method: str, params: dict) -> None:
    await ws.send(json.dumps({"method": method, "params": params, "req_id": req_id}))


async def _await_instrument_snapshot(ws: ClientConnection) -> dict:
    async for raw in ws:
        msg = json.loads(raw)
        if msg.get("method") == "subscribe" and not msg.get("success", True):
            raise RuntimeError(f"instrument subscribe failed: {msg}")
        if msg.get("channel") == "instrument" and msg.get("type") == "snapshot":
            return msg
    raise ConnectionClosed(None, None)


class Recorder:
    def __init__(self, symbols: list[str], depth: int, out_dir: Path) -> None:
        self.symbols = symbols
        self.depth = depth
        self.out_dir = out_dir
        self.backoff_s = 1.0

    async def session(self) -> None:
        """One connection = one file. Runs until cancelled or disconnected."""
        async with connect(URL, max_size=2**25, ping_interval=20) as ws:
            # Precisions first, so the header is guaranteed to be line 1.
            await _request(ws, 1, "subscribe", {"channel": "instrument", "snapshot": True})
            hdr = header(await _await_instrument_snapshot(ws), self.symbols, self.depth)
            await _request(ws, 2, "unsubscribe", {"channel": "instrument"})

            await _request(ws, 3, "subscribe",
                           {"channel": "book", "symbol": self.symbols, "depth": self.depth, "snapshot": True})
            await _request(ws, 4, "subscribe",
                           {"channel": "trade", "symbol": self.symbols, "snapshot": False})

            stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
            path = self.out_dir / f"kraken_{stamp}.jsonl.gz"
            log.info("recording %s (depth %d) -> %s", ", ".join(self.symbols), self.depth, path)

            n_msgs = n_bytes = 0
            last_flush = last_stats = time.monotonic()
            with gzip.open(path, "wt", encoding="utf-8", compresslevel=6) as f:
                f.write(wrap(time.time_ns(), json.dumps(hdr, separators=(",", ":"))))
                async for raw in ws:
                    recv_ns = time.time_ns()
                    if isinstance(raw, bytes):
                        raw = raw.decode()
                    if raw.startswith('{"channel":"heartbeat"'):
                        continue
                    if '"success":false' in raw:
                        raise RuntimeError(f"Kraken rejected a request: {raw}")
                    if '"channel":"instrument"' in raw:
                        continue  # late instrument updates; the header already has what we need
                    f.write(wrap(recv_ns, raw))
                    n_msgs += 1
                    n_bytes += len(raw)
                    self.backoff_s = 1.0  # data is flowing, so the next reconnect can be quick

                    now = time.monotonic()
                    if now - last_flush >= FLUSH_EVERY_S:
                        f.flush()  # sync flush: data so far survives a crash
                        last_flush = now
                    if now - last_stats >= STATS_EVERY_S:
                        log.info("%s: %d msgs, %.1f MB raw", path.name, n_msgs, n_bytes / 1e6)
                        last_stats = now

    async def run(self, duration_s: float | None, rotate_s: float) -> None:
        deadline = None if duration_s is None else time.monotonic() + duration_s
        while True:
            budget = rotate_s if deadline is None else min(rotate_s, deadline - time.monotonic())
            if budget <= 0:
                return
            try:
                async with asyncio.timeout(budget):
                    await self.session()
                reason = "server closed the connection"
            except TimeoutError:
                continue  # rotation or end of --duration; the loop decides which
            except (ConnectionClosed, WebSocketException, OSError) as e:
                reason = f"connection lost ({e})"
            log.warning("%s; reconnecting in %.0fs", reason, self.backoff_s)
            await asyncio.sleep(self.backoff_s)
            self.backoff_s = min(self.backoff_s * 2, 60.0)


async def _main(args: argparse.Namespace) -> None:
    # SIGTERM gets the same clean shutdown as Ctrl-C (closes the gzip stream properly).
    task = asyncio.current_task()
    asyncio.get_running_loop().add_signal_handler(signal.SIGTERM, task.cancel)
    args.out.mkdir(parents=True, exist_ok=True)
    await Recorder(args.symbols, args.depth, args.out).run(args.duration, args.rotate_minutes * 60)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("--symbols", nargs="+", default=["BTC/USD"], help="Kraken v2 pair names, e.g. BTC/USD")
    p.add_argument("--depth", type=int, default=10, choices=BOOK_DEPTHS, help="book levels per side")
    p.add_argument("--out", type=Path, default=Path("data/raw"))
    p.add_argument("--duration", type=float, default=None, help="stop after this many seconds")
    p.add_argument("--rotate-minutes", type=float, default=60.0, help="start a new file this often")
    args = p.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    try:
        asyncio.run(_main(args))
    except (KeyboardInterrupt, asyncio.CancelledError):
        pass
    log.info("stopped")


if __name__ == "__main__":
    main()
