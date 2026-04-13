#!/bin/bash
#SBATCH --job-name=ns3-tcp-sweep
#SBATCH --partition=GPU
#SBATCH --array=0-431%32
#SBATCH --time=00:45:00
#SBATCH --mem=4G
#SBATCH --cpus-per-task=1
#SBATCH --output=experiments/results/raw/slurm-%A_%a.out

# Trace every command so failures are visible in the .out file.
set -x

echo "[task $SLURM_ARRAY_TASK_ID] host=$(hostname) pwd=$(pwd)"

module load gcc python || true
# Source env WITHOUT set -u so any unset vars inside .ns3env don't kill us.
source "$HOME/.ns3env" || { echo "FAIL: sourcing ~/.ns3env"; exit 1; }

if [ -z "${NS3_DIR:-}" ]; then
  echo "FAIL: NS3_DIR not set after sourcing ~/.ns3env" >&2
  exit 1
fi
echo "NS3_DIR=$NS3_DIR"

# Now enable strict mode for the rest of the script.
set -euo pipefail

REPO_ROOT="${SLURM_SUBMIT_DIR:-$(pwd)}"
PARAMS="$REPO_ROOT/experiments/params/parameters.csv"

if [ ! -f "$PARAMS" ]; then
  echo "Missing parameter sweep at $PARAMS — run experiments/scripts/gen_params.py first" >&2
  exit 1
fi

LINE_NUM=$((SLURM_ARRAY_TASK_ID + 2))   # +1 for 1-indexed awk, +1 to skip header
# Strip CRLF in case parameters.csv was written on Windows.
ROW=$(awk -v i="$LINE_NUM" 'NR==i' "$PARAMS" | tr -d '\r')
if [ -z "$ROW" ]; then
  echo "No parameters row for task $SLURM_ARRAY_TASK_ID" >&2
  exit 1
fi

IFS=',' read -r runId algo bwMbps rttMs bufMult nFlows <<< "$ROW"

mkdir -p "$REPO_ROOT/experiments/results/raw"
OUT="$REPO_ROOT/experiments/results/raw/run_${runId}.json"

cd "$NS3_DIR"

# Locate the pre-built tcp-dumbbell binary. Build once before submitting:
#   cd "$NS3_DIR" && ./ns3 configure --enable-examples=false && ./ns3 build
# Array tasks must NOT invoke `./ns3 run` — concurrent tasks race on the
# build/ directory and corrupt CMake's generated files.
# Find the pre-built tcp-dumbbell binary. Use find (robust to empty globs)
# and disable pipefail briefly so a no-match doesn't trip set -e.
set +o pipefail
BIN=$(find build/scratch -maxdepth 3 -type f -name '*tcp-dumbbell*' -executable 2>/dev/null | head -n1 || true)
set -o pipefail

if [ -z "${BIN:-}" ] || [ ! -x "$BIN" ]; then
  echo "FAIL: tcp-dumbbell binary not found under $NS3_DIR/build/scratch" >&2
  echo "Contents of build/scratch:" >&2
  ls -la build/scratch 2>&1 >&2 || true
  echo "Run experiments/scripts/build_ns3.sh on the login node first." >&2
  exit 1
fi
echo "Using binary: $BIN"

"$BIN" \
  --algo=ns3::${algo} \
  --bwMbps=${bwMbps} \
  --rttMs=${rttMs} \
  --bufferBdpMult=${bufMult} \
  --nFlows=${nFlows} \
  --simSec=60 \
  --warmupSec=5 \
  --runId=${runId} \
  --outPath=${OUT}
