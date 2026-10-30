# v0-baseline: Mini-Redis before any performance work

Measured on 2026-10-30 with the scripts in [`bench/`](../../) ([method](../../README.md)).

**Code measured:** the server as of Phase 0 (`80dc7a3`). The commits the scripts ran from (`6c49f01`, `27f933a`, `b1b7828`) add only measurement tools and a `friend` declaration, so all of them build the same server.

**Commit hashes in the raw result files:** after the measurements, the commit messages on this branch were edited, which changed every commit's hash but no file in any commit (each rewritten commit has the same tree as its original). The result files below still show the hashes the scripts recorded at the time:

| Hash in the result files | Same files as |
|---|---|
| `898483c` | `6c49f01` |
| `5eb9969` | `27f933a` |
| `0b58fc1` | `b1b7828` |

**Machine:** Intel Core Ultra 5 125H laptop (hybrid CPU: performance and efficiency cores), 18 logical CPUs, 7.5 GiB, plugged in, Windows "Best performance" mode. Ubuntu 24.04 under WSL2 (kernel 6.6.87.2). GCC 13.3, CMake `RelWithDebInfo` (`-O2 -g`). Real Redis 7.0.15 (Ubuntu package) and redis-benchmark 7.0.15.

| File | Contents | Commit |
|---|---|---|
| [`throughput.md`](throughput.md), [`throughput-runs.csv`](throughput-runs.csv) | Requests/s and latency, Mini-Redis vs Redis; every run | `6c49f01` |
| [`profile/`](profile/profile.md) | Where the CPU time goes: flame graphs and tables | `b1b7828` |
| [`replay.md`](replay.md), [`replay-redis.md`](replay-redis.md) | AOF replay time by file size, Mini-Redis and Redis | `27f933a`, `b1b7828` |
| [`memory-layout.md`](memory-layout.md) | Type sizes and heap bytes per key | `27f933a` |
| [`bug8/`](bug8/) | 1,100 connections against three builds and Redis | `b1b7828` |

## 1. Throughput against Redis

1,000,000 requests per test, 100,000 random keys, 50 clients, 3-byte values. Both servers write an AOF to the OS without `fsync` (or run with it off). Middle of 3 runs; spread is (max − min) / median.

| Test | Mini-Redis req/s (spread) | Redis req/s (spread) | Mini-Redis / Redis |
|---|---:|---:|---:|
| SET, no pipelining, AOF on | 19,336 (7%) | 19,971 (33%) | 0.97× |
| GET, no pipelining, AOF on | 19,868 (2%) | 20,481 (22%) | 0.97× |
| SET, pipeline 16, AOF on | 75,358 (24%) | 173,581 (10%) | **0.43×** |
| GET, pipeline 16, AOF on | 114,103 (3%) | 203,583 (13%) | **0.56×** |
| SET, pipeline 16, AOF off | 124,813 (19%) | 249,626 (16%) | 0.50× |
| GET, pipeline 16, AOF off | 120,978 (0.2%) | 249,439 (13%) | 0.48× |

p99 latency, pipeline 16 with AOF on: SET 25.9 ms vs 7.8 ms, GET 14.5 ms vs 6.6 ms. Without pipelining both servers have a p99 of about 5 ms.

**Without pipelining, the tie says nothing about the code.** Neither server reached 100% CPU (Mini-Redis 83–92%, Redis 76–85%), and neither did the client (135–145% of its 200%). Throughput was set by the cost of each round trip: 37–48 µs of server CPU per request, against 4–13 µs when pipelined, and 60–72% of it in the kernel. That per-round-trip cost (system calls and waking up, in a VM) is the same for both servers.

**With pipelining, the comparison is real.** Mini-Redis ran at 98–102% CPU, so it was the bottleneck, and reached about half of Redis's throughput. CPU per request, from server CPU / throughput (AOF on):

| | Mini-Redis | Redis | Ratio |
|---|---:|---:|---:|
| GET: total | 8.9 µs | 4.6 µs | 1.9× |
| GET: in the kernel | 4.0 µs | 1.3 µs | **3.0×** |
| GET: user space | 4.9 µs | 3.2 µs | 1.5× |
| SET: total | 13.3 µs | 5.5 µs | 2.4× |
| SET: in the kernel | 5.7 µs | 1.1 µs | **5.2×** |
| SET: user space | 7.6 µs | 4.4 µs | 1.7× |

The gap is mostly in the kernel, which points at system calls rather than at the data structures. The profile confirms it.

**The AOF costs Mini-Redis 40% of pipelined SET throughput** (124,813 → 75,358 req/s), against 30% for Redis.

## 2. Where Mini-Redis's CPU time goes

`perf` with kernel frames, 10 s per workload ([profile.md](profile/profile.md), flame graphs: [SET P=1](profile/set-p1.svg), [SET P=16](profile/set-p16.svg), [GET P=16](profile/get-p16.svg)). Inclusive time: the call and everything it does in the kernel.

| Share of server CPU | SET, P=1 | SET, P=16 | GET, P=16 |
|---|---:|---:|---:|
| `send()`, one call per reply | 36% | 24% | **40%** |
| `write()` of AOF records | 11% | 19% | – |
| All AOF logging (`write()` plus building the record) | 21% | **35%** | – |
| `recv()` | 10% | 2% | 4% |
| `select()` | 7% | 0.3% | 1% |
| RESP parsing | 6% | 9% | 14% |
| Hash table lookup, `_M_find_before_node` (self) | 4% | 10% | **17%** |

What each line means, and which planned change addresses it:

- **`send()` per reply, 24–40%.** With pipeline 16, a batch of 16 commands gets 16 `send()` calls. Redis queues replies in a per-client buffer and writes them once per event-loop pass. → Phase 2 output buffer (bug 1) and P3.
- **AOF, 35% of pipelined SET.** One `write()` per command, plus building each record from a copied vector of strings. Removing 35% of the work predicts 1 / (1 − 0.35) = 1.54× with AOF off; the benchmark measured 1.66×. → P1 group commit: one `write()` per event-loop pass, as Redis does.
- **Hash lookups, 10–17%.** Each command looks its key up two or three times: `get()` calls `find()`, then `touchLRU()` does `data_[key]` again; a new key in `SET` adds a lookup of the LRU tail. With 100,000 keys, each lookup chases a pointer to a separately allocated node, likely a cache miss. → P2: one lookup per command.
- **Smaller items** (self time, pipelined): `malloc` and `free` 6–7% (string copies of arguments and replies, → P3); LRU list unlink 2–3% (→ intrusive LRU, Phase 5.2); `toupper` 1.3–1.5%, because the command name is upper-cased twice per command (→ S1 command table); `strtoll` 1.0–1.7% (`std::stoi` in the parser, → change 5); `pthread_rwlock_unlock` 1.4% in GET (the `shared_mutex` a single-threaded server does not need, → Phase 5.4).
- **`select()` is cheap here (under 1.5% when pipelined) with 50 connections.** Its O(N) cost needs many idle connections to show; that is the Phase 4 epoll test.

A first profile run with one redis-benchmark thread (not kept) gave the same shares within 3 percentage points.

## 3. AOF replay is quadratic

Startup time with an AOF of N `SET` commands ([replay.md](replay.md), [replay-redis.md](replay-redis.md)). Same files for both servers; a `GET` after startup checks the whole file was loaded.

| Commands | AOF size | Mini-Redis | Growth exponent | Redis 7.0.15 | Mini-Redis / Redis |
|---:|---:|---:|---:|---:|---:|
| 25,000 | 1.1 MB | 0.39 s | – | 0.05 s | 8× |
| 50,000 | 2.2 MB | 2.2 s | 2.56 | 0.07 s | 34× |
| 100,000 | 4.5 MB | 11.5 s | 2.38 | 0.10 s | 111× |
| 200,000 | 9.0 MB | 46 s | 2.00 | 0.19 s | 246× |
| 400,000 | 18 MB | 174 s | 1.92 | 0.40 s | 438× |
| 1,000,000 | 45 MB | **> 900 s** (stopped) | – | 1.19 s | **> 750×** |

- **Why quadratic:** `replayAOF()` erases each parsed command from the front of a `std::string` holding the rest of the file, so every command moves all the bytes after it. 400,000 commands of 45 bytes move about 3.6 TB in total.
- **The growth exponent** (log of the time ratio over log of the size ratio) settles at about 2. It is above 2 at small sizes because the buffer outgrows the CPU caches there, so each byte moved gets slower. Redis's exponent is 1.1–1.3, roughly linear.
- **Extrapolated,** 1,000,000 commands would take about 18 minutes (174 s × 2.5²). That is an estimate; the run was stopped at the 15-minute cap.
- → Bug 6: parse with a read position instead of erasing.

## 4. Memory per key

From [memory-layout.md](memory-layout.md): 1,000,000 keys like redis-benchmark's (16-byte key, 3-byte value) use **316 bytes of heap per key**, for 19 bytes of data.

| Part | About | Why |
|---|---:|---|
| Hash node | 176 B | the 144-byte map element (key `std::string` + 112-byte `CacheEntry`), plus next pointer, cached hash and malloc header |
| LRU list node | 64 B | a second copy of the key's `std::string`, plus two pointers |
| Key characters on the heap, twice | 64 B | 16 characters is one more than `std::string` stores inline |
| Bucket array | ~12 B | hash table buckets, per key |

The parts are worked out from the type sizes and glibc's allocation rounding (16-byte steps, 8-byte header); together they account for the measured 316 bytes.

`CacheEntry` is 112 bytes because the `std::variant` is as large as its largest alternative, `std::deque` (80 bytes), even when it holds a 3-byte string. → Phase 5.2: store each key once, with the LRU links inside the entry.

## 5. The `select()` limit (bug 8)

[`idle_clients.py`](../../idle_clients.py) opens 1,100 connections, each answering `PING` before the next opens ([bug8/](bug8/)). The server has sockets 0–3 open already, so connection 1,021 gets socket 1024, the first one `fd_set` cannot hold.

| Build | Result at connection 1,021 | Server output |
|---|---|---|
| Optimised (`-O2`; Ubuntu's GCC enables `_FORTIFY_SOURCE=3`) | **Server killed by `SIGABRT`**: every client loses its connection | `*** bit out of range 0 - FD_SETSIZE on fd_set ***: terminated` |
| Debug + ASan/UBSan (`-O0`) | Stopped at the first out-of-bounds access | `Server.cpp:156:49: runtime error: index 16 out of bounds for type 'long int [16]'` |
| Debug, no checks (`-O0`) | **All 1,100 connections answered**, while reading and writing past the end of `fd_set` on the stack | (nothing) |
| Real Redis (`epoll`) | All 1,100 answered | |

- The first out-of-bounds access is a read: `FD_ISSET` at `Server.cpp:156`, in the same loop pass that accepted socket 1024. `FD_SET` at line 116 would write out of bounds on the next pass.
- The unchecked build shows why "it works" proves nothing: undefined behaviour can look harmless in one build and abort in another.
- → Phase 2: refuse connections whose socket number `select()` cannot hold; Phase 4: epoll.

## 6. How far to trust these numbers

- **Run-to-run spread is up to 33% on this machine** (WSL2 on a hybrid CPU: Windows decides which physical core runs each virtual CPU). Compare versions only when the difference is clearly larger than both spreads. Mini-Redis's pipelined GET rows were steady (0.2–3%); its pipelined SET with AOF on was not (24%).
- **Max latency is not meaningful here.** Eight of the sixteen rows, for both servers, show maxima of 210–760 ms (two of them exactly 665.6 ms). Both servers have them, so they are most likely pauses of the VM; they were not investigated further. Use p99.
- **p99 itself is optimistic:** redis-benchmark is closed-loop (coordinated omission).
- **Server CPU above 100%** (up to 102%) is tick-based accounting in a VM; read it as saturated.
- **Loopback only:** there is no real network, and client and server share one machine.
