#!/bin/bash
# Run from the repo root: bash experiments/slurm/build-ns3.sh
set -euo pipefail

module load gcc cmake python || true

NS3_VERSION=3.40
NS3_TARBALL=ns-allinone-${NS3_VERSION}.tar.bz2
NS3_URL=https://www.nsnam.org/releases/${NS3_TARBALL}
NS3_ROOT=$HOME/ns-allinone-${NS3_VERSION}/ns-${NS3_VERSION}

REPO_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SIM_SRC="$REPO_ROOT/experiments/ns3-sim/tcp-dumbbell.cc"

if [ ! -f "$SIM_SRC" ]; then
  echo "Missing $SIM_SRC" >&2
  exit 1
fi

cd "$HOME"
if [ ! -d "ns-allinone-${NS3_VERSION}" ]; then
  echo "Downloading ns-3 ${NS3_VERSION}..."
  wget -q "$NS3_URL"
  tar xjf "$NS3_TARBALL"
fi

cd "$NS3_ROOT"
cp "$SIM_SRC" scratch/
./ns3 configure --build-profile=optimized --enable-examples=false --enable-tests=false
./ns3 build

echo "export NS3_DIR=$NS3_ROOT" > "$HOME/.ns3env"
echo "ns-3 built. Source \$HOME/.ns3env to get NS3_DIR."
