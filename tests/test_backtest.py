import io

import pandas as pd

from mmsim.backtest import Portfolio, merge_events
from mmsim.events import load_book, load_trades, mid_at
from mmsim.fills import Fill
from mmsim.strategy import JoinTouch, MarketState

BOOK_CSV = """recv_ns,exch_ns,bid_px_0,bid_qty_0,ask_px_0,ask_qty_0
100,90,99.0,1.0,101.0,2.0
200,190,99.5,1.0,101.0,2.0
"""
TRADES_CSV = """recv_ns,exch_ns,trade_id,taker_side,price,qty
150,140,1,buy,101.0,0.5
200,195,2,sell,99.5,0.1
"""


def test_portfolio_round_trip_with_fees():
    pf = Portfolio(maker_fee_bps=10)
    pf.apply(Fill(t_ns=1, side="buy", price=100.0, qty=2.0))
    pf.apply(Fill(t_ns=2, side="sell", price=101.0, qty=2.0))
    assert pf.inventory == 0
    assert round(pf.fees, 6) == round((200 + 202) * 10 / 1e4, 6)
    assert round(pf.cash, 6) == round(2.0 - pf.fees, 6)


def test_events_load_and_merge_in_time_order():
    book = load_book(io.StringIO(BOOK_CSV))
    trades = load_trades(io.StringIO(TRADES_CSV))
    assert list(book["mid"]) == [100.0, 100.25]
    assert list(trades["sign"]) == [1, -1]
    kinds = [(k, int(r.recv_ns)) for k, r in merge_events(book, trades)]
    # On a tie at t=200 the trade comes first.
    assert kinds == [("book", 100), ("trade", 150), ("trade", 200), ("book", 200)]


def test_mid_at_uses_last_book_row():
    book = load_book(io.StringIO(BOOK_CSV))
    got = mid_at(book, pd.Series([150, 250, 100]))
    assert list(got) == [100.0, 100.25, 100.0]


def test_join_touch_stops_buying_at_max_inventory():
    s = JoinTouch(size=1.0, max_inventory=2.0)
    state = MarketState(t_ns=0, best_bid=99, best_ask=101, bid_qty=1, ask_qty=1, inventory=2.0)
    q = s.quote(state)
    assert q.bid_px is None
    assert q.ask_px == 101
