#!/usr/bin/env python3
"""Fairly compare several nanobook_bench executables (e.g. before/after an optimization).

Usage:
    python3 scripts/compare.py NAME=PATH [NAME=PATH ...] [--rounds 7] [--commands 1000000] [--seed 42]

Example:
    python3 scripts/compare.py v0=./bench_v0 v1=./build/nanobook_bench

Why this exists: on a normal PC the machine's speed drifts over time (other programs, CPU
boost, thermal limits). If you ran version A now and version B ten minutes later, the
difference could be noise. So we run A, B, A, B, ... (interleaved) several rounds and report
the MEDIAN over rounds plus the min-max spread. A change smaller than the spread is not proof.
"""
import argparse
import re
import statistics
import subprocess
import sys


def run_once(exe, commands, seed):
    out = subprocess.run([exe, str(commands), "--seed", str(seed), "--runs", "3"],
                         capture_output=True, text=True, check=True).stdout
    rate = float(re.search(r"median:\s+([\d.]+)\s+M commands/s", out).group(1))
    row = re.search(r"^ALL\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)", out, re.M)
    _count, avg, p50, p90, p99, p999, mx = (int(g) for g in row.groups())
    return rate, avg, p50, p99, p999


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("versions", nargs="+", help="NAME=PATH of each benchmark executable")
    ap.add_argument("--rounds", type=int, default=7)
    ap.add_argument("--commands", type=int, default=1000000)
    ap.add_argument("--seed", type=int, default=42)
    args = ap.parse_args()

    versions = []
    for item in args.versions:
        if "=" not in item:
            sys.exit(f"expected NAME=PATH, got: {item}")
        name, path = item.split("=", 1)
        versions.append((name, path))

    results = {name: [] for name, _ in versions}
    for r in range(args.rounds):
        for name, path in versions:               # interleaved: A, B, A, B, ...
            results[name].append(run_once(path, args.commands, args.seed))
        print(f"round {r + 1}/{args.rounds} done", file=sys.stderr)

    base = statistics.median(x[0] for x in results[versions[0][0]])
    print(f"\n{args.commands} commands, seed {args.seed}, {args.rounds} interleaved rounds\n")
    print(f"{'version':<14}{'M cmds/s':>9}{'min-max':>14}{'vs first':>10}"
          f"{'avg ns':>8}{'p50':>6}{'p99':>7}{'p99.9':>8}")
    for name, _ in versions:
        rows = results[name]
        rates = [x[0] for x in rows]
        med = statistics.median(rates)
        print(f"{name:<14}{med:>9.2f}{min(rates):>7.2f}-{max(rates):<6.2f}{med / base:>9.2f}x"
              f"{int(statistics.median(x[1] for x in rows)):>8}"
              f"{int(statistics.median(x[2] for x in rows)):>6}"
              f"{int(statistics.median(x[3] for x in rows)):>7}"
              f"{int(statistics.median(x[4] for x in rows)):>8}")
    print("\n(latencies are in ns and include ~30 ns of timer overhead; "
          "p99.9 is noisy, trust throughput and p50/p99 more)")


if __name__ == "__main__":
    main()
