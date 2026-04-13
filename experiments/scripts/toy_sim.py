#!/usr/bin/env python3
"""Toy TCP congestion control simulator — single-bottleneck fluid model.

NOT a substitute for ns-3. Implements simplified Reno, Cubic, BBR, Vegas on
a shared bottleneck at 1-ms granularity. Output JSON matches the shape
produced by experiments/ns3-sim/tcp-dumbbell.cc so aggregate.py/plot.py work
unchanged. Use for quick local smoke tests and pipeline demos, not for
results you'd cite.

Usage (single run):
  python3 toy_sim.py --algo TcpCubic --bwMbps 10 --rttMs 50 \
      --bufferBdpMult 1 --nFlows 2 --simSec 20 --warmupSec 2 \
      --runId smoke --outPath /tmp/smoke.json

Usage (whole sweep):
  python3 toy_sim.py --params experiments/params/parameters_mini.csv \
      --outDir experiments/results/raw --simSec 20 --warmupSec 2
"""
from __future__ import annotations

import argparse
import csv
import json
import math
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List

MSS_BYTES = 1448
DT_MS = 1.0


@dataclass
class Flow:
    rtt_base_ms: float
    cwnd: float = 10.0
    ssthresh: float = 1e9
    acked_bytes: float = 0.0
    acked_bytes_post_warmup: float = 0.0
    # Cubic
    w_max: float = 0.0
    k: float = 0.0
    t_epoch_ms: float = 0.0
    # Vegas / BBR
    min_rtt_ms: float = 1e9
    btl_bw_pkt_per_ms: float = 0.0
    bbr_cycle_idx: int = 0
    bbr_cycle_ms: float = 0.0
    # Delay stats
    delay_weighted_sum: float = 0.0
    delay_weight: float = 0.0
    delay_hist: Dict[int, float] = field(default_factory=dict)


def reno_update(f: Flow, acked: float) -> None:
    if f.cwnd < f.ssthresh:
        f.cwnd += acked
    else:
        f.cwnd += acked / max(f.cwnd, 1.0)


def reno_on_loss(f: Flow) -> None:
    f.cwnd = max(f.cwnd / 2.0, 2.0)
    f.ssthresh = f.cwnd


def cubic_update(f: Flow, t_ms: float, acked: float) -> None:
    C = 0.4
    beta = 0.7
    if f.cwnd < f.ssthresh:
        f.cwnd += acked
        return
    t = max(0.0, (t_ms - f.t_epoch_ms) / 1000.0)
    w_cubic = C * (t - f.k) ** 3 + f.w_max
    rtt_s = max(f.rtt_base_ms / 1000.0, 1e-3)
    w_est = f.w_max * beta + 3 * (1 - beta) / (1 + beta) * (t / rtt_s)
    target = max(w_cubic, w_est)
    if target > f.cwnd:
        f.cwnd += (target - f.cwnd) / max(f.cwnd, 1.0) * acked
    else:
        f.cwnd += acked / max(f.cwnd, 1.0) * 0.1


def cubic_on_loss(f: Flow, t_ms: float) -> None:
    C = 0.4
    beta = 0.7
    f.w_max = f.cwnd
    f.cwnd *= beta
    f.cwnd = max(f.cwnd, 2.0)
    f.ssthresh = f.cwnd
    f.t_epoch_ms = t_ms
    f.k = (max(f.w_max * (1 - beta) / C, 0.0)) ** (1.0 / 3.0)


def vegas_update(f: Flow, rtt_ms: float, acked: float) -> None:
    alpha, beta = 2.0, 4.0
    if f.cwnd < f.ssthresh:
        f.cwnd += acked
        return
    expected = f.cwnd / max(f.min_rtt_ms, 1e-3)
    actual = f.cwnd / max(rtt_ms, 1e-3)
    diff = (expected - actual) * f.min_rtt_ms
    if diff < alpha:
        f.cwnd += acked / max(f.cwnd, 1.0)
    elif diff > beta:
        f.cwnd -= acked / max(f.cwnd, 1.0)
    f.cwnd = max(f.cwnd, 2.0)


def vegas_on_loss(f: Flow) -> None:
    f.cwnd = max(f.cwnd * 0.75, 2.0)
    f.ssthresh = f.cwnd


def bbr_update(f: Flow, rtt_ms: float, acked: float, dt_ms: float) -> None:
    inst_rate = acked / max(dt_ms, 1e-6)
    f.btl_bw_pkt_per_ms = max(f.btl_bw_pkt_per_ms * 0.999, inst_rate)
    f.bbr_cycle_ms += dt_ms
    gains = [1.25, 0.75, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0]
    if f.bbr_cycle_ms >= f.min_rtt_ms:
        f.bbr_cycle_idx = (f.bbr_cycle_idx + 1) % len(gains)
        f.bbr_cycle_ms = 0.0
    gain = gains[f.bbr_cycle_idx]
    bdp_pkts = f.btl_bw_pkt_per_ms * f.min_rtt_ms
    target = max(4.0, bdp_pkts * gain * 2.0)
    f.cwnd += (target - f.cwnd) * 0.1


def bbr_on_loss(f: Flow) -> None:
    f.cwnd = max(f.cwnd * 0.9, 4.0)


def run_sim(algo: str, bw_mbps: float, rtt_ms: float, buffer_bdp_mult: float,
            n_flows: int, sim_sec: float, warmup_sec: float) -> dict:
    bw_pkt_per_ms = bw_mbps * 1e6 / 8.0 / MSS_BYTES / 1000.0
    bdp_bytes = bw_mbps * 1e6 / 8.0 * rtt_ms / 1000.0
    buffer_pkts = max(1, int(buffer_bdp_mult * bdp_bytes / MSS_BYTES))

    flows: List[Flow] = [Flow(rtt_base_ms=rtt_ms) for _ in range(n_flows)]
    stagger_ms = 1000.0 / max(n_flows, 1)
    start_times = [int(i * stagger_ms) for i in range(n_flows)]

    queue_pkts = 0.0
    total_tx = 0.0
    total_lost = 0.0

    sim_ms = int(sim_sec * 1000)
    warmup_ms = warmup_sec * 1000.0
    dt = DT_MS

    for t in range(0, sim_ms, int(dt)):
        q_delay_ms = queue_pkts / max(bw_pkt_per_ms, 1e-9)
        rtt_now = rtt_ms + q_delay_ms

        active = [(i, f) for i, f in enumerate(flows) if t >= start_times[i]]
        offered_rates = []
        total_offered_rate = 0.0
        for _, f in active:
            rate = f.cwnd / max(rtt_now, 1e-3)
            offered_rates.append(rate)
            total_offered_rate += rate

        offered_pkts = total_offered_rate * dt
        capacity_pkts = bw_pkt_per_ms * dt
        served = min(offered_pkts, capacity_pkts + queue_pkts)
        new_queue = queue_pkts + offered_pkts - served
        dropped = 0.0
        if new_queue > buffer_pkts:
            dropped = new_queue - buffer_pkts
            new_queue = float(buffer_pkts)
        queue_pkts = new_queue

        post_warmup = t >= warmup_ms
        if post_warmup:
            total_tx += offered_pkts
            total_lost += dropped

        if offered_pkts > 0:
            for (i, f), rate in zip(active, offered_rates):
                share = rate / total_offered_rate if total_offered_rate > 0 else 0.0
                f_acked = served * share
                f_drop = dropped * share

                f.acked_bytes += f_acked * MSS_BYTES
                if post_warmup:
                    f.acked_bytes_post_warmup += f_acked * MSS_BYTES
                f.min_rtt_ms = min(f.min_rtt_ms, rtt_now)
                f.delay_weighted_sum += rtt_now * f_acked
                f.delay_weight += f_acked
                bin_ms = int(rtt_now)
                f.delay_hist[bin_ms] = f.delay_hist.get(bin_ms, 0.0) + f_acked

                if f_acked > 0:
                    if algo == "TcpCubic":
                        cubic_update(f, t, f_acked)
                    elif algo == "TcpNewReno":
                        reno_update(f, f_acked)
                    elif algo == "TcpVegas":
                        vegas_update(f, rtt_now, f_acked)
                    elif algo == "TcpBbr":
                        bbr_update(f, rtt_now, f_acked, dt)
                if f_drop > 0:
                    if algo == "TcpCubic":
                        cubic_on_loss(f, t)
                    elif algo == "TcpNewReno":
                        reno_on_loss(f)
                    elif algo == "TcpVegas":
                        vegas_on_loss(f)
                    elif algo == "TcpBbr":
                        bbr_on_loss(f)

    measure_sec = max(sim_sec - warmup_sec, 0.1)
    per_flow_mbps = [(f.acked_bytes_post_warmup * 8) / (measure_sec * 1e6) for f in flows]
    total_thr = sum(per_flow_mbps)
    sum_sq = sum(x * x for x in per_flow_mbps)
    jain = (total_thr ** 2 / (len(per_flow_mbps) * sum_sq)) if sum_sq > 0 else 0.0

    mean_delays = [f.delay_weighted_sum / f.delay_weight for f in flows if f.delay_weight > 0]
    mean_delay = sum(mean_delays) / len(mean_delays) if mean_delays else 0.0

    combined: Dict[int, float] = {}
    for f in flows:
        for b, c in f.delay_hist.items():
            combined[b] = combined.get(b, 0.0) + c
    p95 = 0.0
    if combined:
        total = sum(combined.values())
        target = 0.95 * total
        acc = 0.0
        for b in sorted(combined):
            acc += combined[b]
            if acc >= target:
                p95 = float(b)
                break

    loss_rate = total_lost / total_tx if total_tx > 0 else 0.0
    return {
        "totalThroughputMbps": total_thr,
        "perFlowThroughputMbps": per_flow_mbps,
        "meanDelayMs": mean_delay,
        "p95DelayMs": p95,
        "lossRate": loss_rate,
        "jainFairness": jain,
        "queueBytes": buffer_pkts * MSS_BYTES,
    }


def write_json(out_path: Path, run_id: str, algo: str, bw: float, rtt: float,
               buf: float, n: int, sim_sec: float, warmup_sec: float,
               metrics: dict) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "runId": run_id,
        "algo": f"ns3::{algo}",
        "bwMbps": bw,
        "rttMs": rtt,
        "bufferBdpMult": buf,
        "nFlows": n,
        "simSec": sim_sec,
        "warmupSec": warmup_sec,
        **metrics,
    }
    with out_path.open("w") as f:
        json.dump(payload, f)
        f.write("\n")


def run_single(args) -> None:
    algo = args.algo.replace("ns3::", "")
    metrics = run_sim(algo, args.bwMbps, args.rttMs, args.bufferBdpMult,
                      args.nFlows, args.simSec, args.warmupSec)
    write_json(Path(args.outPath), args.runId, algo, args.bwMbps, args.rttMs,
               args.bufferBdpMult, args.nFlows, args.simSec, args.warmupSec,
               metrics)
    print(f"[{args.runId}] {algo}: thr={metrics['totalThroughputMbps']:.2f}Mbps "
          f"delay={metrics['meanDelayMs']:.1f}ms loss={metrics['lossRate']:.2%} "
          f"jain={metrics['jainFairness']:.3f}")


def run_sweep(args) -> None:
    out_dir = Path(args.outDir)
    out_dir.mkdir(parents=True, exist_ok=True)
    with open(args.params) as f:
        rows = list(csv.DictReader(f))
    for i, row in enumerate(rows, 1):
        run_id = row["runId"]
        out_path = out_dir / f"run_{run_id}.json"
        if out_path.exists() and not args.force:
            print(f"[{i}/{len(rows)}] {run_id} already done")
            continue
        algo = row["algo"].replace("ns3::", "")
        metrics = run_sim(
            algo=algo,
            bw_mbps=float(row["bwMbps"]),
            rtt_ms=float(row["rttMs"]),
            buffer_bdp_mult=float(row["bufMult"]),
            n_flows=int(row["nFlows"]),
            sim_sec=args.simSec,
            warmup_sec=args.warmupSec,
        )
        write_json(out_path, run_id, algo, float(row["bwMbps"]),
                   float(row["rttMs"]), float(row["bufMult"]), int(row["nFlows"]),
                   args.simSec, args.warmupSec, metrics)
        print(f"[{i}/{len(rows)}] {run_id} {algo}: "
              f"thr={metrics['totalThroughputMbps']:.2f}Mbps "
              f"delay={metrics['meanDelayMs']:.1f}ms "
              f"jain={metrics['jainFairness']:.3f}")


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("--params", help="Sweep CSV (triggers sweep mode)")
    p.add_argument("--outDir", default="experiments/results/raw")
    p.add_argument("--force", action="store_true")
    p.add_argument("--algo", default="TcpCubic")
    p.add_argument("--bwMbps", type=float, default=10.0)
    p.add_argument("--rttMs", type=float, default=50.0)
    p.add_argument("--bufferBdpMult", type=float, default=1.0)
    p.add_argument("--nFlows", type=int, default=2)
    p.add_argument("--simSec", type=float, default=20.0)
    p.add_argument("--warmupSec", type=float, default=2.0)
    p.add_argument("--runId", default="run")
    p.add_argument("--outPath", default="run.json")
    args = p.parse_args()
    if args.params:
        run_sweep(args)
    else:
        run_single(args)


if __name__ == "__main__":
    main()
