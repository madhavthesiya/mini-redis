# Throughput and latency

- **Date (UTC):** 2026-10-31 17:10
- **Commit:** `3ac13ff8a898` (clean)
- **CPU:** Intel(R) Core(TM) Ultra 5 125H, 18 logical CPUs
- **Memory:** 7.5 GiB
- **Kernel:** Linux 6.6.87.2-microsoft-standard-WSL2 (WSL2: CPUs are virtual, so pinning keeps processes apart but does not fix the physical core)
- **Compiler:** c++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0
- **Build:** CMake `RelWithDebInfo` (`-O2 -g -DNDEBUG`)
- **redis-server:** Redis server v=7.0.15 sha=00000000:0 malloc=jemalloc-5.3.0 bits=64 build=d064f9f48c901a92
- **redis-benchmark:** redis-benchmark 7.0.15
- **taskset:** taskset from util-linux 2.39.3
- **Python:** 3.12.3

- **Load:** `-n 1,000,000` requests per test without pipelining and `-n 5,000,000` with it (redis-benchmark `--threads` rounds each run time up to its 250 ms timer, so fast runs need to be long), `-r 100,000` random keys, `-c 50` clients, 3-byte values; SET then GET on the same fresh server.
- **Pinning:** server on CPU 2, redis-benchmark on CPU 4,6 (2 threads).
- **Runs:** 3 per row, interleaved. The row shows the run with the median throughput; *spread* is (max - min) / median over all runs.
- **Server CPU:** 100% = one core fully busy. *In kernel* is the share of that CPU time spent in system calls (`stime` / (`utime` + `stime`)).
- **Client CPU:** redis-benchmark's CPU use (200% = all 2 threads busy). If the client is near its limit while the server is not, the row measures the client.

Commands (port and directory differ per run):

```
taskset -c 2 build-bench/mini_redis_server --port <port> --maxkeys 200000 --appendonly <yes|no> --appendfilename <tmp>/appendonly.aof
taskset -c 2 redis-server --port <port> --bind 127.0.0.1 --dir <tmp> --save "" --appendonly <yes|no> --appendfsync no --auto-aof-rewrite-percentage 0 --logfile <tmp>/redis.log
taskset -c 4,6 redis-benchmark -h 127.0.0.1 -p <port> -t <set|get> -n <requests> -r 100000 -c 50 -P <pipeline> --csv --threads 2
```

| Server | AOF | Pipeline | Test | Requests/s | Spread | p50 ms | p99 ms | max ms | Server CPU | In kernel | Client CPU |
|---|---|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Mini-Redis | on | 1 | SET | 24,616 | 7.0% | 1.6 | 4.2 | 1088.5 | 82% | 67% | 125% |
| Redis | on | 1 | SET | 25,443 | 5.9% | 1.7 | 4.0 | 32.6 | 75% | 59% | 119% |
| Mini-Redis | on | 1 | GET | 26,291 | 8.2% | 1.6 | 3.8 | 19.5 | 75% | 74% | 125% |
| Redis | on | 1 | GET | 24,450 | 10.2% | 1.7 | 4.0 | 1091.6 | 70% | 67% | 125% |
| Mini-Redis | on | 16 | SET | 172,247 | 6.8% | 3.9 | 9.4 | 28.1 | 103% | 34% | 73% |
| Redis | on | 16 | SET | 213,192 | 9.6% | 3.4 | 6.0 | 1423.4 | 96% | 20% | 59% |
| Mini-Redis | on | 16 | GET | 282,008 | 8.9% | 2.2 | 5.4 | 1467.4 | 102% | 29% | 126% |
| Redis | on | 16 | GET | 293,669 | 5.7% | 2.3 | 5.5 | 22.9 | 97% | 27% | 109% |
| Mini-Redis | off | 1 | SET | 27,184 | 8.4% | 1.5 | 3.8 | 26.4 | 77% | 72% | 129% |
| Redis | off | 1 | SET | 26,638 | 8.4% | 1.6 | 3.8 | 33.5 | 73% | 64% | 129% |
| Mini-Redis | off | 1 | GET | 25,117 | 2.5% | 1.7 | 4.1 | 41.9 | 73% | 74% | 124% |
| Redis | off | 1 | GET | 25,443 | 9.8% | 1.6 | 4.0 | 37.5 | 69% | 67% | 125% |
| Mini-Redis | off | 16 | SET | 285,486 | 7.3% | 2.4 | 5.6 | 23.9 | 106% | 24% | 127% |
| Redis | off | 16 | SET | 285,339 | 11.5% | 2.4 | 5.5 | 15.6 | 93% | 24% | 93% |
| Mini-Redis | off | 16 | GET | 285,209 | 4.2% | 2.4 | 5.8 | 47.3 | 103% | 29% | 134% |
| Redis | off | 16 | GET | 293,634 | 1.5% | 2.4 | 5.0 | 12.8 | 93% | 28% | 102% |

## Mini-Redis relative to Redis

| AOF | Pipeline | Test | Mini-Redis req/s | Redis req/s | Mini-Redis / Redis |
|---|---:|---|---:|---:|---:|
| on | 1 | SET | 24,616 | 25,443 | 0.97x |
| on | 1 | GET | 26,291 | 24,450 | 1.08x |
| on | 16 | SET | 172,247 | 213,192 | 0.81x |
| on | 16 | GET | 282,008 | 293,669 | 0.96x |
| off | 1 | SET | 27,184 | 26,638 | 1.02x |
| off | 1 | GET | 25,117 | 25,443 | 0.99x |
| off | 16 | SET | 285,486 | 285,339 | 1.00x |
| off | 16 | GET | 285,209 | 293,634 | 0.97x |
