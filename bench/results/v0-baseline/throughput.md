# Throughput and latency

- **Date (UTC):** 2026-10-30 20:12
- **Commit:** `898483c48850` (clean)
- **CPU:** Intel(R) Core(TM) Ultra 5 125H, 18 logical CPUs
- **Memory:** 7.5 GiB
- **Kernel:** Linux 6.6.87.2-microsoft-standard-WSL2 (WSL2: CPUs are virtual, so pinning keeps processes apart but does not fix the physical core)
- **Compiler:** c++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
- **Build:** CMake `RelWithDebInfo` (`-O2 -g -DNDEBUG`)
- **redis-server:** Redis server v=7.0.15 sha=00000000:0 malloc=jemalloc-5.3.0 bits=64 build=d064f9f48c901a92
- **redis-benchmark:** redis-benchmark 7.0.15
- **taskset:** taskset from util-linux 2.39.3
- **Python:** 3.12.3

- **Load:** `-n 1,000,000` requests per test, `-r 100,000` random keys, `-c 50` clients, 3-byte values; SET then GET on the same fresh server.
- **Pinning:** server on CPU 2, redis-benchmark on CPU 4,6 (2 threads).
- **Runs:** 3 per row, interleaved. The row shows the run with the median throughput; *spread* is (max - min) / median over all runs.
- **Server CPU:** 100% = one core fully busy. *In kernel* is the share of that CPU time spent in system calls (`stime` / (`utime` + `stime`)).
- **Client CPU:** redis-benchmark's CPU use (200% = all 2 threads busy). If the client is near its limit while the server is not, the row measures the client.

Commands (port and directory differ per run):

```
taskset -c 2 build-bench/mini_redis_server --port <port> --maxkeys 200000 --appendonly <yes|no> --appendfilename <tmp>/appendonly.aof
taskset -c 2 redis-server --port <port> --bind 127.0.0.1 --dir <tmp> --save "" --appendonly <yes|no> --appendfsync no --auto-aof-rewrite-percentage 0 --logfile <tmp>/redis.log
taskset -c 4,6 redis-benchmark -h 127.0.0.1 -p <port> -t <set|get> -n 1000000 -r 100000 -c 50 -P <pipeline> --csv --threads 2
```

| Server | AOF | Pipeline | Test | Requests/s | Spread | p50 ms | p99 ms | max ms | Server CPU | In kernel | Client CPU |
|---|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Mini-Redis | on | 1 | SET | 19,336 | 7.0% | 2.0 | 5.5 | 761.3 | 92% | 65% | 139% |
| Redis | on | 1 | SET | 19,971 | 33.1% | 2.1 | 4.9 | 42.4 | 85% | 60% | 135% |
| Mini-Redis | on | 1 | GET | 19,868 | 1.6% | 2.0 | 5.2 | 20.7 | 84% | 72% | 145% |
| Redis | on | 1 | GET | 20,481 | 21.9% | 2.0 | 4.8 | 25.5 | 76% | 67% | 139% |
| Mini-Redis | on | 16 | SET | 75,358 | 24.1% | 8.7 | 25.9 | 56.0 | 100% | 43% | 83% |
| Redis | on | 16 | SET | 173,581 | 9.5% | 4.3 | 7.8 | 12.3 | 96% | 20% | 60% |
| Mini-Redis | on | 16 | GET | 114,103 | 2.8% | 5.6 | 14.5 | 30.2 | 102% | 45% | 121% |
| Redis | on | 16 | GET | 203,583 | 13.3% | 2.9 | 6.6 | 656.9 | 93% | 29% | 108% |
| Mini-Redis | off | 1 | SET | 20,953 | 3.4% | 1.9 | 4.7 | 688.1 | 84% | 70% | 143% |
| Redis | off | 1 | SET | 20,314 | 3.8% | 2.0 | 4.8 | 665.6 | 78% | 64% | 140% |
| Mini-Redis | off | 1 | GET | 20,483 | 8.1% | 2.0 | 4.9 | 210.6 | 83% | 72% | 144% |
| Redis | off | 1 | GET | 20,950 | 2.7% | 1.9 | 4.7 | 665.6 | 79% | 67% | 144% |
| Mini-Redis | off | 16 | SET | 124,813 | 19.1% | 5.1 | 13.7 | 222.5 | 100% | 40% | 114% |
| Redis | off | 16 | SET | 249,626 | 15.8% | 2.8 | 5.9 | 8.9 | 92% | 22% | 89% |
| Mini-Redis | off | 16 | GET | 120,978 | 0.2% | 5.2 | 13.2 | 217.9 | 98% | 44% | 118% |
| Redis | off | 16 | GET | 249,439 | 12.5% | 2.7 | 5.9 | 8.6 | 91% | 27% | 104% |

## Mini-Redis relative to Redis

| AOF | Pipeline | Test | Mini-Redis req/s | Redis req/s | Mini-Redis / Redis |
|---|---:|---|---:|---:|---:|
| on | 1 | SET | 19,336 | 19,971 | 0.97x |
| on | 1 | GET | 19,868 | 20,481 | 0.97x |
| on | 16 | SET | 75,358 | 173,581 | 0.43x |
| on | 16 | GET | 114,103 | 203,583 | 0.56x |
| off | 1 | SET | 20,953 | 20,314 | 1.03x |
| off | 1 | GET | 20,483 | 20,950 | 0.98x |
| off | 16 | SET | 124,813 | 249,626 | 0.50x |
| off | 16 | GET | 120,978 | 249,439 | 0.48x |
