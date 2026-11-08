"""Stage 3: deciding when your simulated orders get filled.

This is where most backtests lie. With only L2 data you never see your own
order in the queue, so every rule here is an assumption. Make each one
pessimistic, write it down, and test how much P&L depends on it.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Literal

from mmsim.strategy import Quotes

OurSide = Literal["buy", "sell"]


@dataclass(frozen=True)
class Fill:
    t_ns: int
    side: OurSide  # what *we* did: "buy" means our bid was hit
    price: float
    qty: float


@dataclass
class RestingOrder:
    side: OurSide
    price: float
    qty: float  # remaining
    queue_ahead: float  # displayed size in front of us at this price
    live_from_ns: int  # placed + latency; not fillable before this


class ConservativeFillModel:
    """Rules to implement (each one deliberately pessimistic):

    1. Latency. A new quote becomes live ``latency_ns`` after the strategy
       sends it; the old one stays live until then.
    2. Queue position. Joining a level that already shows size S puts you
       behind S: ``queue_ahead = S``. Improving the price (a new level) puts
       you first: ``queue_ahead = 0``.
    3. Trades at your price eat ``queue_ahead`` first; only the remainder can
       fill you.
    4. A trade *through* your price (a buy above your ask, a sell below your
       bid) means your whole level was consumed: you are filled up to the
       trade size.
    5. Size disappearing from your level without a trade is a cancel. Assume
       it came from *behind* you, so ``queue_ahead`` does not shrink.
    6. No fills from book changes alone. Only prints in trades.csv fill you.

    Things to test later: halve the latency, assume cancels come from in front
    of you, and see how much P&L moves. If it moves a lot, the strategy's edge
    is an artifact of the fill model.
    """

    def __init__(self, latency_ns: int = 50_000_000) -> None:
        self.latency_ns = latency_ns
        self.orders: list[RestingOrder] = []

    def on_quotes(self, t_ns: int, quotes: Quotes, book_row) -> None:
        """The strategy wants these quotes. Schedule them per rules 1-2."""
        raise NotImplementedError("stage 3: implement ConservativeFillModel.on_quotes")

    def on_book(self, t_ns: int, book_row) -> None:
        """A book update. Track size at your levels per rule 5."""
        raise NotImplementedError("stage 3: implement ConservativeFillModel.on_book")

    def on_trade(self, t_ns: int, trade_row) -> list[Fill]:
        """A trade printed. Return the fills it gave you per rules 3-4."""
        raise NotImplementedError("stage 3: implement ConservativeFillModel.on_trade")
