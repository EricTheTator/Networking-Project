#!/bin/bash
# Clean rebuild of ns-3 for the tcp-dumbbell sweep.
# Run this once on the login node before `sbatch experiments/slurm/run-array.sbatch`.

set -euo pipefail

module load gcc python || true
source "$HOME/.ns3env"

: "${NS3_DIR:?NS3_DIR must be set (check ~/.ns3env)}"

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SCRATCH_SRC_DIR="$REPO_ROOT/experiments/ns3-sim"

shopt -s nullglob
CC_FILES=("$SCRATCH_SRC_DIR"/*.cc)
shopt -u nullglob

if [ "${#CC_FILES[@]}" -eq 0 ]; then
  echo "ERROR: no .cc sources found under $SCRATCH_SRC_DIR" >&2
  exit 1
fi

cd "$NS3_DIR"

echo ">>> ns-3 dir: $NS3_DIR"

echo ">>> syncing ${#CC_FILES[@]} scratch source(s) from $SCRATCH_SRC_DIR"
mkdir -p scratch
for f in "${CC_FILES[@]}"; do
  echo "    $(basename "$f")"
  cp "$f" "scratch/$(basename "$f")"
done

echo ">>> hard-cleaning previous build (removes stale .so / CMake cache)"
./ns3 clean || true
rm -rf build cmake-cache

echo ">>> configuring (optimized, minimal module set — wifi excluded to avoid stale typeinfo link error)"
# tcp-dumbbell only needs point-to-point + internet stack + apps + flow-monitor
# + traffic-control. Excluding wifi sidesteps the
# `undefined symbol: _ZTIN3ns37WifiMacE` link error entirely.
./ns3 configure -d optimized \
  --disable-examples \
  --disable-tests \
  --enable-modules "core;network;internet;applications;point-to-point;point-to-point-layout;flow-monitor;traffic-control;stats;config-store"

echo ">>> building"
./ns3 build

# Sanity-check every scratch binary produced.
echo ">>> sanity checks"
for src in "${CC_FILES[@]}"; do
  name="$(basename "$src" .cc)"
  BIN=$(find build/scratch -maxdepth 3 -type f -name "*${name}*" -executable 2>/dev/null | head -n1 || true)
  if [ -z "${BIN:-}" ] || [ ! -x "$BIN" ]; then
    echo "ERROR: binary for $name not found under $NS3_DIR/build/scratch" >&2
    exit 1
  fi
  if ldd "$BIN" 2>&1 | grep -qi 'not found'; then
    echo "ERROR: $BIN has unresolved shared libraries:" >&2
    ldd "$BIN" | grep -i 'not found' >&2
    exit 1
  fi
  if ! "$BIN" --PrintHelp >/dev/null 2>sanity.err; then
    echo "ERROR: $BIN failed to start:" >&2
    cat sanity.err >&2
    rm -f sanity.err
    exit 1
  fi
  rm -f sanity.err
  echo "    OK: $BIN"
done

echo ">>> build complete. You can now submit:"
echo "    sbatch experiments/slurm/run-array.sbatch"
