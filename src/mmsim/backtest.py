"""Stage 3: the event loop.

Replays book updates and trades in ``recv_ns`` order. Trades go to the fill
model, which may fill your resting orders; book updates go to the strategy,
which may move its quotes. Equity is marked to the mid after every book event.

Example (once the fill model is implemented)::

    from mmsim import backtest, events
    from mmsim.fills import ConservativeFillModel
    from mmsim.strategy import JoinTouch

    book_csv, trades_csv = events.event_paths("data/events", "kraken_20261108T192622Z", "BTC/USD")
    result = backtest.run(
        events.load_book(book_csv), events.load_trades(trades_csv),
        JoinTouch(size=0.001, max_inventory=0.01), ConservativeFillModel(),
        backtest.Config(maker_fee_bps=0.0),
    )
    print(result.equity.tail())
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Iterator

import pandas as pd

from mmsim.fills import ConservativeFillModel, Fill
from mmsim.strategy import MarketState, Strategy


@dataclass
class Config:
    # Run once at 0 (is there any edge before costs?) and once at your real
    # Kraken fee tier. Retail maker fees are many times BTC/USD's spread.
    maker_fee_bps: float = 0.0
    requote_every_ns: int = 100_000_000  # at most 10 quote changes per second


@dataclass
class Portfolio:
    maker_fee_bps: float
    cash: float = 0.0
    inventory: float = 0.0
    fees: float = 0.0

    def apply(self, f: Fill) -> None:
        notional = f.price * f.qty
        fee = notional * self.maker_fee_bps / 1e4
        if f.side == "buy":
            self.cash -= notional
            self.inventory += f.qty
        else:
            self.cash += notional
            self.inventory -= f.qty
        self.cash -= fee
        self.fees += fee


@dataclass
class Result:
    fills: pd.DataFrame  # t_ns, side, price, qty
    equity: pd.DataFrame  # t_ns, cash, inventory, mid, equity
    fees: float = 0.0
    config: Config = field(default_factory=Config)


def merge_events(book: pd.DataFrame, trades: pd.DataFrame) -> Iterator[tuple[str, object]]:
    """Yield ("trade", row) and ("book", row) in recv_ns order. On a tie the
    trade comes first: it fills against the quotes that were live before it."""
    # (itertuples renames columns that start with "_", so no leading underscore here)
    b = book.assign(ev_kind=1)
    t = trades.assign(ev_kind=0)
    merged = pd.concat([t, b], ignore_index=True).sort_values(["recv_ns", "ev_kind"], kind="stable")
    for row in merged.itertuples(index=False):
        yield ("trade" if row.ev_kind == 0 else "book"), row


def run(book: pd.DataFrame, trades: pd.DataFrame, strategy: Strategy,
        fill_model: ConservativeFillModel, cfg: Config) -> Result:
    pf = Portfolio(cfg.maker_fee_bps)
    fills: list[Fill] = []
    equity_rows: list[tuple[int, float, float, float, float]] = []
    last_quote_ns = -(1 << 62)

    for kind, row in merge_events(book, trades):
        t = int(row.recv_ns)
        if kind == "trade":
            for f in fill_model.on_trade(t, row):
                pf.apply(f)
                fills.append(f)
            continue

        fill_model.on_book(t, row)
        state = MarketState(
            t_ns=t,
            best_bid=row.bid_px_0,
            best_ask=row.ask_px_0,
            bid_qty=row.bid_qty_0,
            ask_qty=row.ask_qty_0,
            inventory=pf.inventory,
        )
        if t - last_quote_ns >= cfg.requote_every_ns:
            fill_model.on_quotes(t, strategy.quote(state), row)
            last_quote_ns = t
        equity_rows.append((t, pf.cash, pf.inventory, state.mid, pf.cash + pf.inventory * state.mid))

    return Result(
        fills=pd.DataFrame([f.__dict__ for f in fills], columns=["t_ns", "side", "price", "qty"]),
        equity=pd.DataFrame(equity_rows, columns=["t_ns", "cash", "inventory", "mid", "equity"]),
        fees=pf.fees,
        config=cfg,
    )
