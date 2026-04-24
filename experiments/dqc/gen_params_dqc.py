#!/usr/bin/env python3
"""Generate the DQC sweep CSV.

Full sweep: 12 algos x 14 instances x 4 loss levels = 672 runs.
Small sweep (--small): 12 algos x 3 instances x 2 loss levels = 72 runs.
"""
from __future__ import annotations

import argparse
import csv
import itertools
from pathlib import Path

ALGOS = [
    "reno", "cubic", "vegas", "bbr", "bbrd", "bbrplus",
    "copa", "pcc", "westwood", "veno", "elastic", "ledbat", "lptcp",
]

INSTANCES_FULL = list(range(1, 15))       # 1..14
INSTANCES_SMALL = [1, 5, 10]

LOSS_FULL = [0, 10, 20, 50]               # per mille => 0%, 1%, 2%, 5%
LOSS_SMALL = [0, 20]


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--small", action="store_true",
                    help="Emit a compact sweep for smoke testing.")
    args = ap.parse_args()

    instances = INSTANCES_SMALL if args.small else INSTANCES_FULL
    losses = LOSS_SMALL if args.small else LOSS_FULL

    repo_root = Path(__file__).resolve().parents[2]
    out_path = repo_root / "experiments" / "params" / "parameters_dqc.csv"
    out_path.parent.mkdir(parents=True, exist_ok=True)

    combos = list(itertools.product(ALGOS, instances, losses))
    width = len(str(len(combos) - 1))

    with out_path.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["runId", "it", "cc", "lo"])
        for i, (cc, it, lo) in enumerate(combos):
            w.writerow([f"dqc{i:0{width}d}", it, cc, lo])

    print(f"Wrote {len(combos)} rows to {out_path}")
    print(f"  algos    = {len(ALGOS)}  {ALGOS}")
    print(f"  instances= {len(instances)} {instances}")
    print(f"  losses   = {len(losses)} {losses} (per mille)")
    print(f"\nTip: set sbatch --array=0-{len(combos)-1}%32 to match this sweep.")


if __name__ == "__main__":
    main()
