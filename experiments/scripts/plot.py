#!/usr/bin/env python3
"""Produce comparison figures from results.parquet (or results.csv)."""
from __future__ import annotations

from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd


def load(results_dir: Path) -> pd.DataFrame:
    parquet = results_dir / "results.parquet"
    if parquet.exists():
        return pd.read_parquet(parquet)
    return pd.read_csv(results_dir / "results.csv")


def plot_throughput_vs_bw(df: pd.DataFrame, out: Path) -> None:
    sub = df[(df["rttMs"] == 50) & (df["bufferBdpMult"] == 1.0)]
    flow_counts = sorted(sub["nFlows"].unique())
    fig, axes = plt.subplots(1, len(flow_counts), figsize=(4 * len(flow_counts), 4),
                             sharey=True, squeeze=False)
    for ax, n in zip(axes[0], flow_counts):
        chunk = sub[sub["nFlows"] == n]
        for algo, g in chunk.groupby("algoShort"):
            g = g.sort_values("bwMbps")
            ax.plot(g["bwMbps"], g["totalThroughputMbps"], marker="o", label=algo)
        ax.set_xscale("log")
        ax.set_xlabel("Bottleneck bandwidth (Mbps)")
        ax.set_title(f"{n} flows")
        ax.grid(True, alpha=0.3)
    axes[0][0].set_ylabel("Total throughput (Mbps)")
    axes[0][-1].legend(loc="best", fontsize=8)
    fig.suptitle("Throughput vs bandwidth (RTT=50ms, buf=1xBDP)")
    fig.tight_layout()
    fig.savefig(out, dpi=150)
    plt.close(fig)


def plot_delay_vs_buffer(df: pd.DataFrame, out: Path) -> None:
    sub = df[(df["bwMbps"] == 10) & (df["nFlows"] == 2) & (df["rttMs"] == 50)]
    algos = sorted(sub["algoShort"].unique())
    fig, axes = plt.subplots(1, len(algos), figsize=(4 * len(algos), 4),
                             sharey=True, squeeze=False)
    for ax, algo in zip(axes[0], algos):
        g = sub[sub["algoShort"] == algo].sort_values("bufferBdpMult")
        ax.plot(g["bufferBdpMult"], g["meanDelayMs"], marker="o", label="mean")
        ax.plot(g["bufferBdpMult"], g["p95DelayMs"], marker="s", label="p95")
        ax.set_xlabel("Buffer (xBDP)")
        ax.set_title(algo)
        ax.grid(True, alpha=0.3)
    axes[0][0].set_ylabel("Queuing delay (ms)")
    axes[0][-1].legend(loc="best", fontsize=8)
    fig.suptitle("Delay vs buffer size (bw=10Mbps, nFlows=2, RTT=50ms)")
    fig.tight_layout()
    fig.savefig(out, dpi=150)
    plt.close(fig)


def plot_fairness_vs_nflows(df: pd.DataFrame, out: Path) -> None:
    sub = df[(df["bwMbps"] == 10) & (df["rttMs"] == 50) & (df["bufferBdpMult"] == 1.0)]
    fig, ax = plt.subplots(figsize=(6, 4))
    for algo, g in sub.groupby("algoShort"):
        g = g.sort_values("nFlows")
        ax.plot(g["nFlows"], g["jainFairness"], marker="o", label=algo)
    ax.set_xscale("log")
    ax.set_xlabel("Number of flows")
    ax.set_ylabel("Jain fairness index")
    ax.set_ylim(0, 1.05)
    ax.grid(True, alpha=0.3)
    ax.legend(loc="best", fontsize=8)
    ax.set_title("Fairness vs flow count (bw=10Mbps, RTT=50ms, buf=1xBDP)")
    fig.tight_layout()
    fig.savefig(out, dpi=150)
    plt.close(fig)


def main() -> None:
    repo_root = Path(__file__).resolve().parents[2]
    results_dir = repo_root / "experiments" / "results"
    figures_dir = results_dir / "figures"
    figures_dir.mkdir(parents=True, exist_ok=True)

    df = load(results_dir)
    plot_throughput_vs_bw(df, figures_dir / "throughput_vs_bandwidth.png")
    plot_delay_vs_buffer(df, figures_dir / "delay_vs_buffer.png")
    plot_fairness_vs_nflows(df, figures_dir / "fairness_vs_nflows.png")
    print(f"Figures written to {figures_dir}")


if __name__ == "__main__":
    main()
