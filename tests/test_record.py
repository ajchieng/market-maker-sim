import json

import pytest

from mmsim.record import header, wrap


def test_wrap_keeps_raw_digits():
    raw = '{"channel":"book","data":[{"qty":0.00006120}]}'
    line = wrap(123, raw)
    assert line.endswith("\n")
    assert "0.00006120" in line  # json.dumps would have written 6.12e-05
    assert json.loads(line) == {"recv_ns": 123, "msg": json.loads(raw)}


def _instrument_snapshot():
    pair = lambda sym, pp: {  # noqa: E731
        "symbol": sym, "price_precision": pp, "qty_precision": 8,
        "price_increment": 10.0 ** -pp, "qty_increment": 1e-8, "qty_min": 0.001, "status": "online",
    }
    return {"channel": "instrument", "type": "snapshot",
            "data": {"assets": [], "pairs": [pair("BTC/USD", 1), pair("ETH/USD", 2), pair("XRP/USD", 5)]}}


def test_header_keeps_only_requested_symbols():
    h = header(_instrument_snapshot(), ["BTC/USD", "ETH/USD"], depth=10)
    assert h["channel"] == "mmsim.header"
    assert h["data"]["book_depth"] == 10
    assert [p["symbol"] for p in h["data"]["pairs"]] == ["BTC/USD", "ETH/USD"]
    assert h["data"]["pairs"][1]["price_precision"] == 2


def test_header_rejects_unknown_symbol():
    with pytest.raises(ValueError, match="BTC/EUR"):
        header(_instrument_snapshot(), ["BTC/EUR"], depth=10)
