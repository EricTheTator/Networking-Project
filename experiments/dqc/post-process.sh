#!/bin/bash
# Run DQC's per-metric post-processing scripts (pro-rate.py, pro-owd.py,
# pro-loss.py, pro-utilit.py) across every algorithm in the sweep.
# Each script expects its input trace files in the CWD and writes into
# ./data_process/. We cd into experiments/results/traces, run the scripts,
# then mirror the outputs into experiments/results/data_process/.
set -euo pipefail

# shellcheck disable=SC1091
source "$HOME/.dqcenv"

REPO_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TRACES="$REPO_ROOT/experiments/results/traces"
DATA_OUT="$REPO_ROOT/experiments/results/data_process"
DQC_SCRIPTS="$NS3_DIR/src/dqc/script"

if [ ! -d "$TRACES" ]; then
  echo "No traces found at $TRACES. Run the sweep first." >&2
  exit 1
fi

mkdir -p "$DATA_OUT"

ALGOS=(reno cubic vegas bbr bbrd bbrplus copa pcc westwood veno elastic ledbat lptcp)

cd "$TRACES"
for cc in "${ALGOS[@]}"; do
  for stage in pro-rate.py pro-owd.py pro-loss.py pro-utilit.py; do
    if [ -f "$DQC_SCRIPTS/$stage" ]; then
      echo "[$cc] $stage"
      python "$DQC_SCRIPTS/$stage" --algo="$cc" || \
        echo "warn: $stage failed for $cc" >&2
    fi
  done
done

# DQC scripts write into ./data_process/; move into repo results dir.
if [ -d "$TRACES/data_process" ]; then
  cp -f "$TRACES/data_process/"* "$DATA_OUT/" 2>/dev/null || true
fi

echo "Post-process outputs in $DATA_OUT"
