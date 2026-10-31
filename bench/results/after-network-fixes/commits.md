# Throughput by commit

- **Date (UTC):** 2026-10-31 17:10
- **Commit:** `3ac13ff8a898` (clean)
- **CPU:** Intel(R) Core(TM) Ultra 5 125H, 18 logical CPUs
- **Memory:** 7.5 GiB
- **Kernel:** Linux 6.6.87.2-microsoft-standard-WSL2 (WSL2: CPUs are virtual, so pinning keeps processes apart but does not fix the physical core)
- **Python:** 3.12.3

- **Method:** each commit's own `bench.py --servers mini --pipelines 16 --aof on,off --runs 1 --requests 5000000`, commits measured in turn, 3 rounds. Requests/s is the median of the rounds, with (max - min) / median in brackets.

| Version | Commit |
|---|---|
| before | `6d76fd2 refactor(server): move socket portability to Socket.h, waiting to a Poller` |
| reply-buffers | `cd9760f fix(server): never block on one client: non-blocking sockets, reply buffers` |
| final | `2c888d3 perf(server): read into a scratch buffer, not straight into the client's` |

| AOF | Pipeline | Test | before | reply-buffers | final | final / before |
|---|---:|---|---:|---:|---:|---:|
| on | 16 | SET | 124,116 (152%) | 181,699 (19%) | 179,986 (5%) | 1.45x |
| on | 16 | GET | 175,144 (24%) | 289,503 (23%) | 293,755 (9%) | 1.68x |
| off | 16 | SET | 162,380 (14%) | 281,373 (8%) | 277,516 (7%) | 1.71x |
| off | 16 | GET | 170,224 (15%) | 301,841 (14%) | 283,543 (7%) | 1.67x |

Median server CPU per row (100% = one core busy):

| AOF | Pipeline | Test | before | reply-buffers | final |
|---|---:|---|---:|---:|---:|
| on | 16 | SET | 100% | 99% | 101% |
| on | 16 | GET | 96% | 96% | 98% |
| off | 16 | SET | 98% | 98% | 99% |
| off | 16 | GET | 98% | 104% | 99% |
