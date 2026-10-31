#!/usr/bin/env python3
"""Mini-Redis throughput at several commits, measured in one interleaved session.

Each commit is checked out into its own git worktree and measured with that
commit's own bench.py (Mini-Redis only). The commits are measured in turn,
round after round, so slow drift such as a laptop heating up affects all of
them alike. The table gives the median of the rounds with its spread, and how
the last commit compares with the first.

Numbers from different sessions are not comparable on a noisy machine; this
puts every version in the same session, which is what a before/after claim
for a single commit needs.

Usage (from the repository root, on Linux):
  python3 bench/compare_commits.py --out bench/results/<name>/commits.md \\
      before=<commit> after=<commit> [more=<commit> ...]
"""

import argparse
import csv
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

import benchlib as bl


def parse_args():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("versions", nargs="+", help="label=commit, in the order to show them")
    p.add_argument("--out", required=True, type=Path, help="Markdown file to write")
    p.add_argument("--rounds", type=int, default=3)
    p.add_argument("--pipelines", default="16")
    p.add_argument("--aof", default="on,off")
    # Long runs: with --threads, redis-benchmark rounds each run time up to its
    # 250 ms timer (see bench.py), which matters for fast pipelined runs
    p.add_argument("--requests", type=int, default=5_000_000)
    args = p.parse_args()
    args.versions = [v.split("=", 1) for v in args.versions]
    if any(len(v) != 2 for v in args.versions) or len(args.versions) < 2:
        p.error("give at least two versions as label=commit")
    return args


def git(*argv):
    return subprocess.run(["git", "-C", str(bl.REPO_ROOT), *argv], check=True,
                          capture_output=True, text=True).stdout.strip()


def main():
    args = parse_args()
    if args.out.exists():
        sys.exit(f"{args.out} already exists; pick another --out or delete it")
    # Not /tmp: under WSL, systemd empties it whenever the distribution restarts.
    work = Path.home() / ".cache" / "mini-redis-compare" / time.strftime("%Y%m%d-%H%M%S")
    work.mkdir(parents=True)

    versions = []   # (label, worktree, commit description)
    try:
        for label, commit in args.versions:
            tree = work / label
            git("worktree", "add", "--detach", str(tree), commit)
            if not (tree / "bench" / "bench.py").exists():
                sys.exit(f"{commit} has no bench/bench.py")
            versions.append((label, tree, git("log", "-1", "--format=%h %s", commit)))

        for round_ in range(1, args.rounds + 1):
            for label, tree, _ in versions:
                result = subprocess.run(
                    [sys.executable, str(tree / "bench" / "bench.py"),
                     "--out", str(work / f"{label}-r{round_}"), "--servers", "mini",
                     "--pipelines", args.pipelines, "--aof", args.aof, "--runs", "1",
                     "--requests", str(args.requests)],
                    capture_output=True, text=True)
                if result.returncode != 0:
                    sys.exit(f"bench.py failed for {label}:\n{result.stderr[-2000:]}")
            print(f"round {round_}/{args.rounds} done", file=sys.stderr, flush=True)

        rps, cpu = {}, {}
        for label, _, _ in versions:
            for f in work.glob(f"{label}-r*/throughput-runs.csv"):
                for r in csv.DictReader(open(f)):
                    key = (r["aof"], int(r["pipeline"]), r["test"], label)
                    rps.setdefault(key, []).append(float(r["rps"]))
                    cpu.setdefault(key, []).append(float(r["server_cpu_pct"]))
    finally:
        for _, tree, _ in versions:
            subprocess.run(["git", "-C", str(bl.REPO_ROOT), "worktree", "remove", "--force",
                            str(tree)], capture_output=True)

    labels = [label for label, _, _ in versions]
    lines = ["# Throughput by commit", "",
             bl.environment_markdown(bl.environment()), "",
             f"- **Method:** each commit's own `bench.py --servers mini --pipelines "
             f"{args.pipelines} --aof {args.aof} --runs 1 --requests {args.requests}`, "
             f"commits measured in turn, {args.rounds} rounds. Requests/s is the median of "
             "the rounds, with (max - min) / median in brackets.",
             "", "| Version | Commit |", "|---|---|"]
    lines += [f"| {label} | `{desc}` |" for label, _, desc in versions]
    lines += ["", "| AOF | Pipeline | Test | " + " | ".join(labels) + f" | {labels[-1]} / {labels[0]} |",
              "|---|---:|---|" + "---:|" * (len(labels) + 1)]
    for aof in args.aof.split(","):
        for pipeline in (int(p) for p in args.pipelines.split(",")):
            for test in ("SET", "GET"):
                cells, medians = [], []
                for label in labels:
                    values = rps[(aof, pipeline, test, label)]
                    m = statistics.median(values)
                    medians.append(m)
                    cells.append(f"{m:,.0f} ({100 * (max(values) - min(values)) / m:.0f}%)")
                lines.append(f"| {aof} | {pipeline} | {test} | " + " | ".join(cells)
                             + f" | {medians[-1] / medians[0]:.2f}x |")
    lines += ["", "Median server CPU per row (100% = one core busy):", "",
              "| AOF | Pipeline | Test | " + " | ".join(labels) + " |",
              "|---|---:|---|" + "---:|" * len(labels)]
    for (aof, pipeline, test) in sorted({k[:3] for k in cpu}, key=lambda k: (k[0] == "off", k[1], k[2] != "SET")):
        lines.append(f"| {aof} | {pipeline} | {test} | " + " | ".join(
            f"{statistics.median(cpu[(aof, pipeline, test, l)]):.0f}%" for l in labels) + " |")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
