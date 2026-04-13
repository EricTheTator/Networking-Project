#!/usr/bin/env python3
"""Aggregate per-run JSONs into a single parquet/CSV table."""
from __future__ import annotations

import json
from pathlib import Path

import pandas as pd


def main() -> None:
    repo_root = Path(__file__).resolve().parents[2]
    raw_dir = repo_root / "experiments" / "results" / "raw"
    out_dir = repo_root / "experiments" / "results"
    out_dir.mkdir(parents=True, exist_ok=True)

    rows = []
    for p in sorted(raw_dir.glob("run_*.json")):
        try:
            with p.open() as f:
                rows.append(json.load(f))
        except (json.JSONDecodeError, OSError) as e:
            print(f"skip {p.name}: {e}")

    if not rows:
        print("No run_*.json files found.")
        return

    df = pd.DataFrame(rows)
    df["algoShort"] = df["algo"].str.replace("ns3::Tcp", "", regex=False)

    parquet_path = out_dir / "results.parquet"
    csv_path = out_dir / "results.csv"
    try:
        df.to_parquet(parquet_path, index=False)
        print(f"Wrote {parquet_path} ({len(df)} rows)")
    except Exception as e:
        print(f"parquet write failed ({e}); CSV only")
    df.drop(columns=["perFlowThroughputMbps"], errors="ignore").to_csv(csv_path, index=False)
    print(f"Wrote {csv_path}")


if __name__ == "__main__":
    main()
