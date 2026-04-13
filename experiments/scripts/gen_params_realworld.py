#!/usr/bin/env python3
"""Generate a sweep CSV using real-world network profiles.

Each profile captures a representative bandwidth / RTT / buffer / flow-count
combination for a specific deployment (home cable, LTE, Starlink, etc.).
Every profile is crossed with all four TCP congestion control algorithms, so
the resulting sweep is small (profiles x algos) and directly comparable.
"""
from __future__ import annotations

import csv
from dataclasses import dataclass
from pathlib import Path
from typing import List

ALGOS = ["TcpBbr", "TcpCubic", "TcpNewReno", "TcpVegas"]


@dataclass
class Profile:
    name: str
    description: str
    bwMbps: float
    rttMs: float
    bufMult: float
    nFlows: int


PROFILES: List[Profile] = [
    Profile("home_cable", "Residential cable: 100Mbps down, ~20ms to CDN",
            bwMbps=100, rttMs=20, bufMult=1.0, nFlows=4),
    Profile("home_fiber", "Gigabit fiber to the home, low latency",
            bwMbps=1000, rttMs=10, bufMult=1.0, nFlows=4),
    Profile("rural_dsl", "Rural DSL, congested uplink",
            bwMbps=5, rttMs=40, bufMult=2.0, nFlows=2),
    Profile("mobile_lte", "Mobile LTE — moderate speed, deep buffers (bufferbloat)",
            bwMbps=20, rttMs=70, bufMult=4.0, nFlows=2),
    Profile("mobile_5g", "Mid-band 5G — fast and low-latency",
            bwMbps=200, rttMs=30, bufMult=1.0, nFlows=4),
    Profile("starlink_leo", "LEO satellite (Starlink) — high bw, variable RTT",
            bwMbps=100, rttMs=50, bufMult=1.0, nFlows=2),
    Profile("geo_satellite", "Geostationary satellite — high BDP link",
            bwMbps=10, rttMs=600, bufMult=1.0, nFlows=1),
    Profile("datacenter_lan", "Intra-datacenter — high bw, microsecond RTT",
            bwMbps=1000, rttMs=2, bufMult=0.5, nFlows=8),
    Profile("transatlantic_wan", "Transatlantic WAN between data centers",
            bwMbps=1000, rttMs=80, bufMult=1.0, nFlows=4),
    Profile("congested_wifi", "Home wifi, shallow buffers, shared",
            bwMbps=30, rttMs=15, bufMult=0.5, nFlows=8),
]


def main() -> None:
    repo_root = Path(__file__).resolve().parents[2]
    out_path = repo_root / "experiments" / "params" / "parameters_realworld.csv"
    out_path.parent.mkdir(parents=True, exist_ok=True)

    with out_path.open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["runId", "algo", "bwMbps", "rttMs", "bufMult", "nFlows"])
        for profile in PROFILES:
            for algo in ALGOS:
                run_id = f"rw_{profile.name}_{algo}"
                w.writerow([run_id, algo, profile.bwMbps, profile.rttMs,
                            profile.bufMult, profile.nFlows])

    print(f"Wrote {len(PROFILES) * len(ALGOS)} rows across "
          f"{len(PROFILES)} profiles x {len(ALGOS)} algorithms to {out_path}")
    print("\nProfiles:")
    for p in PROFILES:
        print(f"  {p.name:20s} {p.bwMbps:>5.0f} Mbps  {p.rttMs:>4.0f} ms  "
              f"buf={p.bufMult}xBDP  flows={p.nFlows}  — {p.description}")


if __name__ == "__main__":
    main()
