#!/usr/bin/env python3
"""Plot real-world profile results as grouped bar charts (one bar per algo)."""
from __future__ import annotations

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


METRICS = [
    ("totalThroughputMbps", "Total throughput (Mbps)", False),
    ("meanDelayMs", "Mean delay (ms)", True),
    ("p95DelayMs", "p95 delay (ms)", True),
    ("jainFairness", "Jain fairness", False),
    ("lossRate", "Loss rate", True),
]


def load(results_dir: Path) -> pd.DataFrame:
    parquet = results_dir / "results.parquet"
    if parquet.exists():
        df = pd.read_parquet(parquet)
    else:
        df = pd.read_csv(results_dir / "results.csv")
    df = df[df["runId"].astype(str).str.startswith("rw_")].copy()
    if df.empty:
        raise SystemExit("No rw_* runs in results. Did you run the realworld sweep?")
    parts = df["runId"].str.split("_", n=2, expand=True)
    df["scenario"] = parts[1] + "_" + parts[2].str.rsplit("_", n=1).str[0]
    df["algoShort"] = df["algo"].str.replace("ns3::Tcp", "", regex=False)
    return df


def plot_metric(df: pd.DataFrame, metric: str, label: str,
                log_scale: bool, out_path: Path) -> None:
    scenarios = sorted(df["scenario"].unique())
    algos = sorted(df["algoShort"].unique())
    pivot = df.pivot_table(index="scenario", columns="algoShort",
                           values=metric, aggfunc="mean").reindex(scenarios)

    x = np.arange(len(scenarios))
    width = 0.8 / len(algos)
    fig, ax = plt.subplots(figsize=(max(8, len(scenarios) * 1.2), 5))
    for i, algo in enumerate(algos):
        if algo not in pivot.columns:
            continue
        offsets = x + (i - (len(algos) - 1) / 2) * width
        ax.bar(offsets, pivot[algo].values, width, label=algo)

    ax.set_xticks(x)
    ax.set_xticklabels(scenarios, rotation=35, ha="right")
    ax.set_ylabel(label)
    ax.set_title(f"{label} by real-world network profile")
    ax.grid(True, axis="y", alpha=0.3)
    if log_scale:
        ax.set_yscale("symlog" if metric == "lossRate" else "log")
    ax.legend(title="CC algorithm", loc="best", fontsize=8)
    fig.tight_layout()
    fig.savefig(out_path, dpi=150)
    plt.close(fig)


def main() -> None:
    repo_root = Path(__file__).resolve().parents[2]
    results_dir = repo_root / "experiments" / "results"
    figures_dir = results_dir / "figures"
    figures_dir.mkdir(parents=True, exist_ok=True)

    df = load(results_dir)
    for metric, label, log_scale in METRICS:
        if metric not in df.columns:
            continue
        out = figures_dir / f"realworld_{metric}.png"
        plot_metric(df, metric, label, log_scale, out)
        print(f"Wrote {out}")


if __name__ == "__main__":
    main()
