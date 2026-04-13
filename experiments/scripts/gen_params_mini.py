#!/usr/bin/env python3
"""Small local sweep: 4 algos x 2 bw x 2 rtt x 2 buf x 2 flows = 64 runs."""
from __future__ import annotations

import csv
import itertools
from pathlib import Path

ALGOS = ["TcpBbr", "TcpCubic", "TcpNewReno", "TcpVegas"]
BW_MBPS = [10, 100]
RTT_MS = [10, 50]
BUFFER_BDP = [1.0, 4.0]
N_FLOWS = [1, 4]


def main() -> None:
    repo_root = Path(__file__).resolve().parents[2]
    out_path = repo_root / "experiments" / "params" / "parameters_mini.csv"
    out_path.parent.mkdir(parents=True, exist_ok=True)

    combos = list(itertools.product(ALGOS, BW_MBPS, RTT_MS, BUFFER_BDP, N_FLOWS))
    width = len(str(len(combos) - 1))

    with out_path.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["runId", "algo", "bwMbps", "rttMs", "bufMult", "nFlows"])
        for i, (algo, bw, rtt, buf, n) in enumerate(combos):
            w.writerow([f"mini{i:0{width}d}", algo, bw, rtt, buf, n])

    print(f"Wrote {len(combos)} rows to {out_path}")


if __name__ == "__main__":
    main()
