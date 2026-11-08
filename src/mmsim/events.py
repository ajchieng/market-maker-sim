"""Load the event files written by ``lob_replay`` into pandas.

``lob_replay`` writes, per symbol:

* ``<stamp>.<SYMBOL>.book.csv``: a row each time the top N levels change.
  Columns ``recv_ns, exch_ns, bid_px_0, bid_qty_0, ..., ask_px_0, ask_qty_0, ...``
* ``<stamp>.<SYMBOL>.trades.csv``: ``recv_ns, exch_ns, trade_id, taker_side, price, qty``.
  ``taker_side`` is the aggressor: "buy" means someone lifted the ask.

Clock rule: drive the simulation with ``recv_ns``, the moment *you* would have
known about an event. ``exch_ns`` comes from Kraken's clock, ``recv_ns`` from
yours, and they disagree by milliseconds (sometimes recv_ns < exch_ns), so
never subtract one from the other without estimating the offset first.
"""

from __future__ import annotations

from pathlib import Path

import pandas as pd


def event_paths(events_dir: Path | str, stem: str, symbol: str) -> tuple[Path, Path]:
    """(book.csv, trades.csv) for one recording stem and symbol like "BTC/USD"."""
    base = Path(events_dir) / f"{stem}.{symbol.replace('/', '-')}"
    return Path(f"{base}.book.csv"), Path(f"{base}.trades.csv")


def load_book(path: Path | str) -> pd.DataFrame:
    df = pd.read_csv(path)
    df["ts"] = pd.to_datetime(df["recv_ns"], unit="ns", utc=True)
    df["mid"] = (df["bid_px_0"] + df["ask_px_0"]) / 2
    df["spread"] = df["ask_px_0"] - df["bid_px_0"]
    return df


def load_trades(path: Path | str) -> pd.DataFrame:
    df = pd.read_csv(path)
    df["ts"] = pd.to_datetime(df["recv_ns"], unit="ns", utc=True)
    df["sign"] = df["taker_side"].map({"buy": 1, "sell": -1})
    return df


def mid_at(book: pd.DataFrame, t_ns: pd.Series) -> pd.Series:
    """Mid price in force at each time in ``t_ns`` (last book row at or before it)."""
    left = pd.DataFrame({"t_ns": t_ns.to_numpy()}).reset_index()
    joined = pd.merge_asof(
        left.sort_values("t_ns"),
        book[["recv_ns", "mid"]].sort_values("recv_ns"),
        left_on="t_ns",
        right_on="recv_ns",
        direction="backward",
    )
    return joined.sort_values("index")["mid"].set_axis(t_ns.index)
