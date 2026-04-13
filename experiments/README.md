# TCP Congestion Control Sweep (ns-3 on UNCC Centaurus)

Simulates a dumbbell topology in ns-3 and compares `TcpBbr`, `TcpCubic`,
`TcpNewReno`, and `TcpVegas` across a sweep of bandwidth / RTT / buffer size /
flow count. Runs as a Slurm job array.

## Layout

```
experiments/
  ns3-sim/tcp-dumbbell.cc   # the simulation (goes into ns-3 scratch/)
  slurm/build-ns3.sh        # user-space ns-3 build
  slurm/run-array.sbatch    # Slurm array (432 tasks)
  scripts/gen_params.py     # emit params/parameters.csv
  scripts/aggregate.py      # raw JSONs -> results.{parquet,csv}
  scripts/plot.py           # figures
```

Sweep: 4 algos x 3 bw {1,10,100 Mbps} x 3 RTT {10,50,150 ms}
x 3 buffer {0.5,1,4 x BDP} x 4 flows {1,2,8,32} = **432 runs**.

## Workflow

1. **Build ns-3** (once, on a login node):
   ```
   bash experiments/slurm/build-ns3.sh
   source ~/.ns3env    # sets NS3_DIR
   ```
2. **Smoke test one combo:**
   ```
   "$NS3_DIR"/ns3 run "scratch/tcp-dumbbell --algo=ns3::TcpCubic \
     --bwMbps=10 --rttMs=50 --bufferBdpMult=1 --nFlows=2 \
     --simSec=20 --warmupSec=2 --runId=smoke --outPath=/tmp/smoke.json"
   cat /tmp/smoke.json
   ```
3. **Generate the parameter sweep:**
   ```
   python experiments/scripts/gen_params.py
   ```
4. **Submit the job array** from the repo root:
   ```
   sbatch experiments/slurm/run-array.sbatch
   squeue -u $USER
   ```
5. **Aggregate & plot when done:**
   ```
   python experiments/scripts/aggregate.py
   python experiments/scripts/plot.py
   ```
   Figures land in `experiments/results/figures/`.

## Per-run metrics (JSON)

Each task writes `experiments/results/raw/run_<id>.json` with: total and
per-flow throughput (Mbps), mean + p95 queuing delay (ms), loss rate, and
Jain fairness index.

## Notes

- The three bundled subprojects (`PCC-Kernel-master`, `bbr-master`,
  `remy-master`) are left untouched — they're background/reference material.
  PCC and BBR kernel modules can't be loaded on the shared HPC cluster; ns-3's
  built-in userspace implementations are used instead.
- Tune `--array=0-431%32` concurrency to whatever Centaurus policy allows.
- If `./ns3 build` fails with GCC-version errors, try `module load gcc/11.x`
  before running `build-ns3.sh`.
