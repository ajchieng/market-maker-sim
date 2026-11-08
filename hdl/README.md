# Stretch goal: an ITCH 5.0 parser in hardware

Do this after stage 2 works. Same idea as the C++ book, but in SystemVerilog,
on the feed format real US exchanges use.

**Goal:** a streaming parser that takes Nasdaq TotalView-ITCH 5.0 bytes (say 8
bytes per clock), finds message boundaries and decodes Add Order (`A`), Order
Executed (`E`), Order Cancel (`X`) and Delete Order (`D`) into fixed-width
fields. Compare its clocks per message with a C++ parser of the same data.

## Pieces

- **Spec:** Nasdaq's "TotalView-ITCH 5.0" specification PDF (message layouts,
  big-endian fields, 2-byte length prefix per message in the sample files).
- **Data:** Nasdaq publishes full-day ITCH 5.0 sample files (several GB each,
  gzipped). Cut the first few MB for testing.
- **Simulator:** Verilator (`brew install verilator`). No FPGA board needed.
- **Testbench:** cocotb (`uv add --dev cocotb`) lets you write the testbench in
  Python: feed real bytes in, compare decoded fields with a Python reference
  parser.

## Milestones

1. Python reference parser for the four message types; counts per type for the sample.
2. SystemVerilog framer: length prefix -> message boundaries, one message per clock out.
3. Field decoder for `A`/`E`/`X`/`D`; cocotb test matches the Python parser on 1M messages.
4. Report: messages per clock at the target width, resources if synthesized
   (Yosys gives a rough LUT count for free), and the C++ comparison.
