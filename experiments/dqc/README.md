# TCP Congestion Control Sweep via DrainQueueCongestion

This replaces the earlier ns-3.40 pipeline. It uses
[DrainQueueCongestion](https://github.com/SoonyangZhang/DrainQueueCongestion)
(DQC) — an ns-3.30 module that ships userspace implementations of Reno, Cubic,
Vegas, BBR, BBRv2-like (`bbrd`), BBRPlus, COPA, PCC, Westwood, Veno, Elastic,
LEDBAT, and TCP-LP, all selectable from one binary (`scratch/dqc-test`) via
`--cc=<name>`.

## Layout

```
experiments/
  dqc/
    build-dqc.sh       # one-shot: fetch ns-3.30, clone DQC, configure + build
    gen_params_dqc.py  # write parameters_dqc.csv (runId, it, cc, lo)
    run-array.sbatch   # Slurm array; one row per task
    post-process.sh    # wraps DQC's pro-rate.py / pro-owd.py / pro-loss.py / pro-utilit.py
    plot-all.sh        # wraps DQC's bw_plot.sh over every (instance, algo)
  params/parameters_dqc.csv
  results/
    slurm/             # Slurm stdout/stderr per task
    traces/            # raw DQC trace files (copied from $NS3_DIR/traces)
    data_process/      # aggregated rate / owd / loss / utilization outputs
    figures/           # PNGs from bw_plot.sh
```

## Workflow

### 1. One-time build on Centaurus

```
bash experiments/dqc/build-dqc.sh
```

This writes `$HOME/.dqcenv` with `NS3_DIR`, `DQC`, and `CPLUS_INCLUDE_PATH`.
Source it in later shells: `source ~/.dqcenv`.

### 2. Smoke test

```
source ~/.dqcenv
cd "$NS3_DIR" && ./waf --run "scratch/dqc-test --it=1 --cc=reno --lo=0"
ls "$NS3_DIR/traces/"       # expect 1_reno_* files
```

### 3. Generate the sweep

```
python experiments/dqc/gen_params_dqc.py            # full: 728 rows (13 algos x 14 instances x 4 losses)
python experiments/dqc/gen_params_dqc.py --small    # smoke: 78 rows
```

The generator prints the matching `--array=0-N%32` line to use. Edit
`run-array.sbatch`'s `#SBATCH --array=` to match before submitting.

### 4. Submit

```
sbatch experiments/dqc/run-array.sbatch
squeue -u "$USER"
```

Or dry-run task 0 interactively:

```
SLURM_ARRAY_TASK_ID=0 SLURM_SUBMIT_DIR=$PWD bash experiments/dqc/run-array.sbatch
```

Each task runs `scratch/dqc-test --it=<it> --cc=<cc> --lo=<lo>` and copies
matching traces from `$NS3_DIR/traces/` into `experiments/results/traces/`.

### 5. Post-process

```
bash experiments/dqc/post-process.sh
```

Runs DQC's `pro-rate.py`, `pro-owd.py`, `pro-loss.py`, `pro-utilit.py` for
every algorithm in the sweep. Outputs land in `experiments/results/data_process/`.

### 6. Plot

```
bash experiments/dqc/plot-all.sh
```

By default plots instances `1 5 10` against all 13 algorithms. Override with
env vars:

```
INSTANCES="1 5 10 14" ALGOS="bbr bbrd copa pcc" bash experiments/dqc/plot-all.sh
```

PNGs land in `experiments/results/figures/` — one per (instance, algo) pair,
each showing per-flow bandwidth + one-way delay timelines.

## Algorithms

| algo       | notes                                     |
|------------|-------------------------------------------|
| reno       | classic AIMD                              |
| cubic      | Linux default                             |
| vegas      | delay-based                               |
| bbr        | Google BBR v1                             |
| bbrd       | DQC's BBRv2-like variant (ECN-aware)      |
| bbrplus    | BBRPlus                                   |
| copa       | Arun & Balakrishnan                       |
| pcc        | PCC Allegro                               |
| westwood   | bandwidth-estimate TCP                    |
| veno       | wireless-oriented Reno variant            |
| elastic    | Elastic-TCP                               |
| ledbat     | low extra-delay background transport      |
| lptcp      | TCP-LP low-priority                       |

Multipath variants (`liaen`, `olia`, `balia`, coupled BBR) require the
`parking-lot.cc` topology and are **not** in this sweep. Research-prototype
algos (`learning`, `hunnan*`, `viva*`, `lpbbr`, `liaen2`) are omitted; smoke
each one with `scratch/dqc-test --cc=<name>` before adding to the sweep.

## Risks

- **GCC version**: ns-3.30 needs gcc <= 10. `build-dqc.sh` tries
  `gcc/9.3.0` first then falls back to the default `gcc` module.
- **`CPLUS_INCLUDE_PATH` pollution**: the exports live in `~/.dqcenv`, not
  `~/.bashrc`, so they apply only when sourced.
- **Trace filename pattern**: DQC's naming isn't fully documented; if
  `post-process.sh` or the `cp` glob in `run-array.sbatch` misses files, run
  the smoke test and adjust the glob accordingly.
