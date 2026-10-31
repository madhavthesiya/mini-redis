# After the network fixes (Phase 2)

Measured on 2026-10-31 with the scripts in [`bench/`](../../) ([method](../../README.md)), on the same machine as [v0-baseline](../v0-baseline/README.md): Intel Core Ultra 5 125H laptop, Ubuntu 24.04 under WSL2, GCC 13 `-O2`, real Redis 7.0.15. The laptop was plugged in with other applications closed.

**Code measured:** the server at `2c888d3`. The throughput runs were made from `3ac13ff`, which only adds a benchmark change on top, so it builds the same server.

| File | Contents |
|---|---|
| [`commits.md`](commits.md) | Before and after the event-loop rewrite, measured in one interleaved session |
| [`throughput.md`](throughput.md), [`throughput-runs.csv`](throughput-runs.csv) | Mini-Redis against Redis, all tests, every run |
| [`profile/`](profile/profile.md) | Where the CPU time goes now (`perf`, flame graphs) |

## 1. Before and after, same session

Pipeline 16, 5,000,000 requests per test, 100,000 random keys, 50 clients. Middle of 3 interleaved rounds (spread in brackets). "Before" is the commit just before the event-loop rewrite: the new parser is already in, but replies still go out one `send()` per command.

| Test | Before | + reply buffers (`cd9760f`) | Final (`2c888d3`) | Final / before |
|---|---:|---:|---:|---:|
| SET, AOF on | 124,116 (152%) | 181,699 (19%) | 179,986 (5%) | **1.45×** |
| GET, AOF on | 175,144 (24%) | 289,503 (23%) | 293,755 (9%) | **1.68×** |
| SET, AOF off | 162,380 (14%) | 281,373 (8%) | 277,516 (7%) | **1.71×** |
| GET, AOF off | 170,224 (15%) | 301,841 (14%) | 283,543 (7%) | **1.67×** |

The server was at 96–104% CPU in every row, so each row measures the server.

- **Almost all of the gain comes from the reply buffers.** The replies to every command in one read now leave in one `send()`, where there used to be one call per command. In the v0 profile, `send()` took 24–40% of the CPU.
- **The three later performance commits made no measurable difference here:** the read position, the backlog with the accept loop and 16 KB reads, and the scratch-buffer read. The final column is within noise of the reply-buffer column. At pipeline 16 a read holds about 16 commands (under 1 KB), so erasing per command moved little. Accepts happen only when connections open. A 4 KB read already held a whole batch. These changes matter for other loads, such as deep pipelines, bursts of connections and large values, which this benchmark does not exercise.
- **The scratch-buffer read fixed a regression.** Reading straight into the client's `std::string` zero-filled 16 KB per read; the profile showed that `memset` at 2.4% of CPU. Reading into a stack buffer and appending only the bytes that arrived removes it.
- **One outlier:** the "before" SET with AOF on has a 152% spread, because one round was slow; its median is still the middle run.

## 2. Against real Redis, same session

[`throughput.md`](throughput.md): 1,000,000 requests per test without pipelining, 5,000,000 with. Both servers write their AOF to the OS without `fsync` (or have it off).

| Test | Mini-Redis req/s | Redis req/s | Mini-Redis / Redis | Same ratio at v0 |
|---|---:|---:|---:|---:|
| SET, no pipelining, AOF on | 24,616 | 25,443 | 0.97× | 0.97× |
| GET, no pipelining, AOF on | 26,291 | 24,450 | 1.08× | 0.97× |
| SET, pipeline 16, AOF on | 172,247 | 213,192 | **0.81×** | 0.43× |
| GET, pipeline 16, AOF on | 282,008 | 293,669 | **0.96×** | 0.56× |
| SET, pipeline 16, AOF off | 285,486 | 285,339 | **1.00×** | 0.50× |
| GET, pipeline 16, AOF off | 285,209 | 293,634 | **0.97×** | 0.48× |

Pipelined, the two servers now reach similar throughput, but they don't do the same work for it. Mini-Redis ran at 102–106% CPU and Redis at 93–97%, while the client had room to spare (59–134% of its 200%). So the fairer measure is CPU per request, server CPU ÷ requests/s:

| Pipeline 16 | Mini-Redis | Redis | Mini-Redis needs |
|---|---:|---:|---:|
| SET, AOF on | 6.0 µs | 4.5 µs | 1.33× |
| GET, AOF on | 3.6 µs | 3.3 µs | 1.10× |
| SET, AOF off | 3.7 µs | 3.3 µs | 1.14× |
| GET, AOF off | 3.6 µs | 3.2 µs | 1.14× |

- **The remaining gap is mostly the AOF.** Mini-Redis makes one `write()` per command; Redis writes once per event-loop pass. That is the next planned speed change (group commit, Phase 4).
- **The kernel share is now about the same as Redis's:** 24–29% for Mini-Redis against 24–28% for Redis, except SET with AOF on (34% against 20%). At v0 it was 40–45% against 20–29%.
- **Without pipelining, both servers still tie, and that still says nothing about the code.** Neither reaches 100% CPU; the cost of each round trip sets the pace (as at v0).
- p99 latency at pipeline 16: 5.4–9.4 ms for Mini-Redis, 5.0–6.0 ms for Redis. The 9.4 ms is SET with AOF on.

## 3. Where the CPU goes now

[`profile/`](profile/profile.md), at the final code. Inclusive share of the server's CPU, compared with v0:

| | SET, P=1 | SET, P=16 | GET, P=16 |
|---|---:|---:|---:|
| `send()` | 37% (v0 36%) | **10%** (v0 24%) | **23%** (v0 40%) |
| `write()` of AOF records | 11% (11%) | 25% (19%) | – |
| All AOF logging | 20% (21%) | **43%** (35%) | – |
| RESP parsing | 4% (6%) | 6% (9%) | 10% (14%) |
| Hash table lookup (self) | – | 15% (10%) | **27%** (17%) |

These are shares of a smaller total. Each request now costs less overall, so a cost that stayed the same takes a larger share: the AOF and the hash lookups didn't get slower. They are now the largest costs:
- **AOF logging, 43% of pipelined SET:** one `write()` per command, and the record built from copied strings. Planned: group commit (Phase 4).
- **Hash lookups, 15–27%:** two or three lookups per command. Planned: one lookup per command (Phase 4).

## 4. How far to trust these numbers

- **This session was quiet:** spreads were 1.5–11.5% in the full benchmark, and 5–24% in the commit comparison apart from the one outlier. Two earlier attempts the same day, with other applications running, had spreads of up to 83% and 102%, and their numbers were discarded.
- **Compare within a session, not across sessions.** Redis's own GET at pipeline 16 measured 204k req/s in the v0 session and 294k here: the machine's speed varies between sessions by more than most code changes. Mini-Redis's before/after numbers therefore come from one interleaved session (section 1), and the "same ratio at v0" column compares ratios, not raw numbers.
- **Run times are long enough now.** With `--threads`, redis-benchmark rounds every run time up to a 250 ms tick. The pipelined runs here lasted 16–29 s, so that error is about 1.5% at most. The v0 ratios at pipeline 16 were affected more and flatter Mini-Redis by a few percent (see the [v0 correction note](../v0-baseline/README.md#6-how-far-to-trust-these-numbers)).
- **Max latency again shows rare ~1–1.5 s outliers for both servers** (for example 1,424 ms for Redis's SET at pipeline 16). As at v0, these are most likely pauses of the VM; use p99.
- **Unchanged since v0:** AOF replay is still quadratic (fixed in Phase 3), and memory per key is still 316 bytes.
