#!/bin/bash
# Run a parameter sweep locally (no Slurm). Usage:
#   bash experiments/scripts/run_local.sh [params.csv] [simSec]
set -euo pipefail

: "${NS3_DIR:?Set NS3_DIR to your ns-3 build directory}"

REPO_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PARAMS=${1:-"$REPO_ROOT/experiments/params/parameters_mini.csv"}
SIM_SEC=${2:-20}
WARMUP_SEC=2

mkdir -p "$REPO_ROOT/experiments/results/raw"

total=$(( $(wc -l < "$PARAMS") - 1 ))
i=0
tail -n +2 "$PARAMS" | while IFS=, read -r runId algo bwMbps rttMs bufMult nFlows; do
  i=$((i+1))
  OUT="$REPO_ROOT/experiments/results/raw/run_${runId}.json"
  if [ -f "$OUT" ]; then
    echo "[$i/$total] $runId already done, skipping"
    continue
  fi
  echo "[$i/$total] $runId: $algo bw=$bwMbps rtt=$rttMs buf=$bufMult n=$nFlows"
  (cd "$NS3_DIR" && ./ns3 run "scratch/tcp-dumbbell \
    --algo=ns3::${algo} --bwMbps=${bwMbps} --rttMs=${rttMs} \
    --bufferBdpMult=${bufMult} --nFlows=${nFlows} \
    --simSec=${SIM_SEC} --warmupSec=${WARMUP_SEC} \
    --runId=${runId} --outPath=${OUT}") > /dev/null
done

echo "Done. Aggregate with: python experiments/scripts/aggregate.py"
