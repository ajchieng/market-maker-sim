"""Stage 4: where did the P&L come from?

A market maker earns the spread and loses to adverse selection: the trades
that hit you tend to be the ones that know where the price is going next. The
two functions below separate those effects. Their output, more than the final
P&L number, is what to show in the write-up.
"""

from __future__ import annotations

import pandas as pd

from mmsim.events import mid_at  # noqa: F401  (you'll want it)

HORIZONS_S = (1, 5, 10, 30, 60)


def markouts(fills: pd.DataFrame, book: pd.DataFrame, horizons_s=HORIZONS_S) -> pd.DataFrame:
    """Markout of each fill at each horizon, in price units per unit traded.

        sign       = +1 for our buys, -1 for our sells
        markout_h  = sign * (mid(t + h) - fill_price)

    At h = 0 this is the half-spread you captured (positive). As h grows it
    usually falls, and how far it falls is adverse selection. Plot the mean
    markout against h, separately for buys and sells. If it goes negative
    within a second, you are being picked off.

    Returns one row per fill with columns markout_1s, markout_5s, ...
    """
    raise NotImplementedError("stage 4: implement markouts (hint: mid_at with t_ns + h * 1e9)")


def pnl_decomposition(fills: pd.DataFrame, book: pd.DataFrame, final_mid: float, fees: float,
                      horizon_s: float = 10) -> dict[str, float]:
    """Split total P&L into parts that add up exactly:

        spread_capture     sum(sign * (mid_at_fill - price) * qty)      >= 0 for passive fills
        adverse_selection  sum(sign * (mid(t+h) - mid_at_fill) * qty)   usually < 0
        inventory          the rest: P&L from holding the position after h
        fees               -fees

    Check: the four parts sum to the backtester's final equity.
    """
    raise NotImplementedError("stage 4: implement pnl_decomposition")
