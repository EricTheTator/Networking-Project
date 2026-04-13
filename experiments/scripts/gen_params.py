#!/usr/bin/env python3
"""Generate the parameter sweep CSV for the Slurm job array.

Writes experiments/params/parameters.csv with one row per (algo, bw, rtt,
buf, nFlows) combination. Row index == SLURM_ARRAY_TASK_ID.
"""
from __future__ import annotations

import csv
import itertools
from pathlib import Path

ALGOS = ["TcpBbr", "TcpCubic", "TcpNewReno", "TcpVegas"]
BW_MBPS = [1, 10, 100]
RTT_MS = [10, 50, 150]
BUFFER_BDP = [0.5, 1.0, 4.0]
N_FLOWS = [1, 2, 8, 32]


def main() -> None:
    repo_root = Path(__file__).resolve().parents[2]
    out_path = repo_root / "experiments" / "params" / "parameters.csv"
    out_path.parent.mkdir(parents=True, exist_ok=True)

    combos = list(itertools.product(ALGOS, BW_MBPS, RTT_MS, BUFFER_BDP, N_FLOWS))
    width = len(str(len(combos) - 1))

    with out_path.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["runId", "algo", "bwMbps", "rttMs", "bufMult", "nFlows"])
        for i, (algo, bw, rtt, buf, n) in enumerate(combos):
            w.writerow([f"{i:0{width}d}", algo, bw, rtt, buf, n])

    print(f"Wrote {len(combos)} rows to {out_path}")


if __name__ == "__main__":
    main()
