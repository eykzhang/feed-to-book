# feed-to-book

A market-data feed handler and limit order book in C++, measured end to end from packet arrival to book update.

The input is Nasdaq TotalView-ITCH 5.0: full-day historical sample files that record every add, cancel, and execution for every symbol, in the binary format Nasdaq sends its feed in. A replayer sends a recorded day over UDP multicast with MoldUDP64 framing, at its original timing. The receiving side treats that as a live feed. It sequences packets, detects gaps, recovers them the way MoldUDP64 specifies (a UDP re-request for small gaps, a snapshot over TCP for a late join), decodes messages, and maintains a per-symbol order book.

The headline number is wire-to-book latency, reported as a distribution (p50, p99, p99.9). It runs from the kernel's receive timestamp on the packet to the completed book update, so it excludes time on the wire and in the NIC.

## Status

Scoped, not built. No code yet.

## Roadmap

1. **File replay.** Parse ITCH 5.0 from a sample file and build a book per symbol. Validate against trade prints in the same file.
2. **Book design and benchmarks.** A benchmark harness and several book layouts compared under real message flow: tree of price levels, flat array indexed by price tick, intrusive per-level order lists.
3. **Networking.** Multicast replayer, sequence-gap detection, UDP re-request and snapshot recovery. Done when a full day replayed with injected packet loss produces the same book as the file replay.
4. **Pipeline concurrency.** Receive, decode, and book stages on pinned cores, connected by a hand-written lock-free SPSC ring buffer, compared against a single-threaded baseline.
5. **Stretch: the exchange side.** A matching engine behind a TCP order-entry gateway, publishing its own ITCH-style feed.

## Out of scope

Kernel bypass (DPDK, ef_vi, Onload), FPGAs and hardware-timestamping NICs, the live Nasdaq feed, and multiple venues.

## Platform

Phases 1 and 2 run anywhere. Phases 3 and 4 target Linux, which the multicast setup, `perf`, core isolation, and busy-poll socket options depend on. Every published number names the machine and date it came from, and the raw benchmark output lives under `results/`.
