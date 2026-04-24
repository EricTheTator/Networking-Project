#!/bin/bash
# Emit one PNG per (instance, algo) pair using DQC's bw_plot.sh.
# bw_plot.sh is hardcoded to a single (instance, algo) via two top-of-file
# variables; we copy it to /tmp and sed-substitute per iteration.
set -euo pipefail

# shellcheck disable=SC1091
source "$HOME/.dqcenv"

REPO_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TRACES="$REPO_ROOT/experiments/results/traces"
FIGS="$REPO_ROOT/experiments/results/figures"
DQC_BW="$NS3_DIR/src/dqc/script/bw_plot.sh"

INSTANCES=(${INSTANCES:-1 5 10})
ALGOS=(${ALGOS:-reno cubic vegas bbr bbrd bbrplus copa pcc westwood veno elastic ledbat lptcp})

if [ ! -d "$TRACES" ]; then
  echo "No traces at $TRACES; run the sweep first." >&2
  exit 1
fi
if [ ! -f "$DQC_BW" ]; then
  echo "Missing $DQC_BW; did build-dqc.sh clone the repo?" >&2
  exit 1
fi

mkdir -p "$FIGS"

for it in "${INSTANCES[@]}"; do
  for algo in "${ALGOS[@]}"; do
    TMP=$(mktemp --suffix=_bw_plot.sh)
    cp "$DQC_BW" "$TMP"
    sed -i "s/^instance=.*/instance=${it}/" "$TMP"
    sed -i "s/^algo=.*/algo=${algo}/" "$TMP"
    (cd "$TRACES" && bash "$TMP") || \
      echo "warn: bw_plot.sh failed for it=${it} algo=${algo}" >&2
    rm -f "$TMP"
  done
done

# bw_plot.sh writes PNGs into CWD (= $TRACES); move them to figures/
find "$TRACES" -maxdepth 1 -name "*.png" -exec mv -t "$FIGS" {} +

echo "Figures in $FIGS"
