"""Stage 3: quoting strategies.

A strategy sees the market and its own inventory and decides where to rest one
bid and one ask. It never sees fills directly; the backtester feeds them back
through ``MarketState.inventory``.
"""

from __future__ import annotations

import math  # noqa: F401  (AvellanedaStoikov needs math.log)
from dataclasses import dataclass
from typing import Protocol


@dataclass(frozen=True)
class MarketState:
    t_ns: int
    best_bid: float
    best_ask: float
    bid_qty: float  # size displayed at the best bid
    ask_qty: float
    inventory: float  # base units you hold; negative = short
    # Add what your strategy needs: rolling volatility, order book imbalance,
    # recent trade flow, time since the last fill, ...

    @property
    def mid(self) -> float:
        return (self.best_bid + self.best_ask) / 2

    @property
    def spread(self) -> float:
        return self.best_ask - self.best_bid


@dataclass(frozen=True)
class Quotes:
    """Where to rest orders. A None price means no order on that side."""

    bid_px: float | None
    bid_qty: float
    ask_px: float | None
    ask_qty: float


class Strategy(Protocol):
    def quote(self, state: MarketState) -> Quotes: ...


@dataclass
class JoinTouch:
    """Baseline: join the best bid and ask with a fixed size, stop quoting the
    side that would push inventory past ``max_inventory``.

    It is here to show the interface and to give you something to beat. On a
    tight book like BTC/USD it gets picked off whenever the price moves.
    """

    size: float
    max_inventory: float

    def quote(self, state: MarketState) -> Quotes:
        can_buy = state.inventory + self.size <= self.max_inventory
        can_sell = state.inventory - self.size >= -self.max_inventory
        return Quotes(
            bid_px=state.best_bid if can_buy else None,
            bid_qty=self.size,
            ask_px=state.best_ask if can_sell else None,
            ask_qty=self.size,
        )


@dataclass
class AvellanedaStoikov:
    """Avellaneda & Stoikov (2008), "High-frequency trading in a limit order book".

    With mid ``s``, inventory ``q`` and time left ``tau = T - t``:

        reservation price   r = s - q * gamma * sigma**2 * tau
        total spread        delta = gamma * sigma**2 * tau + (2 / gamma) * ln(1 + gamma / k)
        quotes              bid = r - delta / 2,  ask = r + delta / 2

    Long inventory pulls both quotes down (you'd rather sell); short pulls them
    up. Crypto never closes, so hold ``tau`` fixed (a rolling horizon) instead
    of letting it run to zero.

    Parameters you must estimate from your own data, not guess:
      sigma  volatility of the mid, in price units per sqrt(second)
      k      how fast fill probability decays with distance from mid:
             fill intensity lambda(delta) = A * exp(-k * delta). Fit it from
             how often trades reach each distance from the mid.
      gamma  risk aversion. Sweep it and plot P&L against inventory variance.
    """

    gamma: float
    k: float
    sigma: float
    tau_s: float
    size: float
    tick: float
    max_inventory: float

    def quote(self, state: MarketState) -> Quotes:
        # TODO (stage 3):
        #   1. r and delta from the formulas above
        #   2. round bid down / ask up to self.tick
        #   3. never cross the book: bid <= best_bid... or decide deliberately
        #      when to improve the touch (bid between best_bid and mid)
        #   4. respect max_inventory like JoinTouch does
        raise NotImplementedError("stage 3: implement AvellanedaStoikov.quote")
