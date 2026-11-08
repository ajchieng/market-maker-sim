# Newest recording by default; override with `make replay RECORDING=data/raw/....jsonl.gz`
RECORDING ?= $(lastword $(sort $(wildcard data/raw/*.jsonl.gz)))
SYMBOL ?= BTC/USD
MINUTES ?= 10

.PHONY: setup record build test replay bench clean

setup:  ## install Python deps, configure the C++ build (downloads simdjson + Catch2)
	uv sync
	cmake -S cpp -B build

record:  ## record MINUTES of BTC/USD and ETH/USD into data/raw/
	uv run mm-record --symbols BTC/USD ETH/USD --duration $$(( $(MINUTES) * 60 ))

build:
	cmake --build build

test: build  ## Python tests, then C++ tests (the [book] ones fail until stage 2 is done)
	uv run pytest -q
	./build/lob_tests

replay: build  ## rebuild the book from RECORDING, check every checksum, write data/events/
	./build/lob_replay $(RECORDING) --out data/events

bench: build  ## ns per level update on RECORDING
	./build/lob_bench $(RECORDING) --symbol $(SYMBOL)

clean:
	rm -rf build
