# Stress Test Results

Screenshots and evidence from running Mini-Redis stress tests.

## Tests Performed
- **Unit Tests**: 55 test cases covering all data types, TTL, LRU eviction, persistence, AOF and the RESP parser
- **Thread Safety**: 5 tests, each running 8 threads at once against one storage instance (20,000 operations in total)
- **Multi-Client**: Multiple `redis-cli` clients connected to the server concurrently via TCP

## Preliminary Windows benchmarks (superseded)

These came from an early run on Windows, before the measurement method in [`bench/`](../bench/) existed. They are kept as a record but are **not comparable** with the Linux baseline in [`bench/results/v0-baseline/`](../bench/results/v0-baseline/):
- compiler flags were not recorded (the CMake build then set no optimisation level);
- `redis-benchmark` used its defaults (50 clients, 3-byte values) with no `-r`, so every request hit the **same key**, the best case for hash lookups and CPU caches;
- AOF on, every write flushed to the OS; one run per test.

| Test | Command | SET (req/s) | GET (req/s) |
|------|---------|:-----------:|:-----------:|
| No pipelining | `redis-benchmark -p 6379 -t set,get -n 100000 -q` | 17,394 | 20,555 |
| Pipeline depth 16 | `redis-benchmark -p 6379 -t set,get -n 100000 -P 16 -q` | 225,734 | 224,719 |
| Pipeline depth 50 | `redis-benchmark -p 6379 -t set,get -n 100000 -P 50 -q` | 609,756 | 487,805 |

- Other commands (`-t set,get,lpush,rpush,lpop,rpop,sadd,hset -n 100000 -q`): LPUSH 22,041 · LPOP 22,401 · SADD 24,431 req/s.
- Single client (`-t set,get -n 10000 -c 1`): SET 18,315 req/s, 99.97% within 1 ms; GET 32,154 req/s, all within 1 ms (1 ms resolution).

## Screenshots

| File | What it shows |
|------|---------------|
| `Single-Command Throughput.png` | `-t set,get -n 10000 -q`, a multi-command run (`-n 100000`), and single-client latency (`-c 1 -n 10000`) |
| `Pipelined Throughput.png` | `-t set,get -n 100000` without pipelining, then with `-P 16` and `-P 50` |
| `Real Redis 7.png` | Real Redis on port 6380, `-t set,get -n 100000 -q`; its configuration (e.g. whether AOF was on) was not recorded, so it is not a like-for-like comparison |
