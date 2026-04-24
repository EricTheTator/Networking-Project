#!/bin/bash
#SBATCH --job-name=dqc-sweep
#SBATCH --partition=Centaurus
#SBATCH --array=0-727%32
#SBATCH --time=00:30:00
#SBATCH --mem=2G
#SBATCH --cpus-per-task=1
#SBATCH --output=experiments/results/slurm/%A_%a.out

# Runs one row of experiments/params/parameters_dqc.csv per array task.
# Usage (submit): sbatch experiments/dqc/run-array.sbatch
# Usage (dry-run one task):
#   SLURM_ARRAY_TASK_ID=0 SLURM_SUBMIT_DIR=$PWD bash experiments/dqc/run-array.sbatch
set -euo pipefail

module load gcc/9.3.0 python3 2>/dev/null || module load gcc python3

# shellcheck disable=SC1091
source "$HOME/.dqcenv"   # sets NS3_DIR, DQC, CPLUS_INCLUDE_PATH

: "${SLURM_SUBMIT_DIR:?Must be set (auto-set by sbatch, export manually for dry-run)}"
: "${SLURM_ARRAY_TASK_ID:?Must be set}"

PARAMS="${SLURM_SUBMIT_DIR}/experiments/params/parameters_dqc.csv"
# skip the header, then grab row (SLURM_ARRAY_TASK_ID + 1)
ROW=$(awk -v i=$((SLURM_ARRAY_TASK_ID + 2)) 'NR==i' "$PARAMS")
if [ -z "$ROW" ]; then
  echo "No row at SLURM_ARRAY_TASK_ID=$SLURM_ARRAY_TASK_ID in $PARAMS" >&2
  exit 1
fi
IFS=',' read -r runId it cc lo <<< "$ROW"

echo "[${runId}] it=${it} cc=${cc} lo=${lo}"

mkdir -p "${SLURM_SUBMIT_DIR}/experiments/results/slurm"

cd "$NS3_DIR"
./waf --run "scratch/dqc-test --it=${it} --cc=${cc} --lo=${lo}"

# Mirror this task's traces back into the repo results dir.
# DQC filename pattern (observed): {it}_{cc}[loss_suffix]_{flow}_{bw|owd|...}.txt
DEST="${SLURM_SUBMIT_DIR}/experiments/results/traces"
mkdir -p "$DEST"
cp "$NS3_DIR/traces/${it}_${cc}"* "$DEST/" 2>/dev/null || \
  echo "warn: no traces matched ${it}_${cc}* after run" >&2
