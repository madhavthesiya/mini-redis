# Stress Test Results

Screenshots and evidence from running Mini-Redis stress tests.

## Tests Performed
- **Unit Tests**: 55 test cases covering all data types, TTL, LRU eviction, persistence, AOF and the RESP parser
- **Thread Safety**: 5 tests, each running 8 threads at once against one storage instance (20,000 operations in total)
- **Multi-Client**: Multiple `redis-cli` clients connected to the server concurrently via TCP

## Screenshots

Preliminary `redis-benchmark` runs on Windows. All runs used the defaults (50 clients, 3-byte values, no `-r`, so every request hit the same key), and the compiler flags were not recorded. See the main README's Benchmark section for caveats; these are being re-measured on Linux.

| File | What it shows |
|------|---------------|
| `Single-Command Throughput.png` | `-t set,get -n 10000 -q`, a multi-command run (`-n 100000`), and single-client latency (`-c 1 -n 10000`) |
| `Pipelined Throughput.png` | `-t set,get -n 100000` without pipelining, then with `-P 16` and `-P 50` |
| `Real Redis 7.png` | Real Redis on port 6380, `-t set,get -n 100000 -q`; its configuration (e.g. whether AOF was on) was not recorded, so it is not a like-for-like comparison |
