#!/bin/bash
# One-shot build for DrainQueueCongestion on top of ns-3.30.
# Run once on a Centaurus login node (or an salloc shell).
set -euo pipefail

module load gcc/9.3.0 python3 cmake 2>/dev/null || module load gcc python3 cmake

cd "$HOME"

NS3_ALLINONE="$HOME/ns-allinone-3.30"
NS3_DIR="$NS3_ALLINONE/ns-3.30"
DQC_INC="$NS3_DIR/src/dqc/model/thirdparty"

# 1. ns-3.30 tarball
if [ ! -d "$NS3_ALLINONE" ]; then
  wget https://www.nsnam.org/releases/ns-allinone-3.30.tar.bz2
  tar xjf ns-allinone-3.30.tar.bz2
fi

# 2. Clone DQC as src/dqc
if [ ! -d "$NS3_DIR/src/dqc" ]; then
  git clone https://github.com/SoonyangZhang/DrainQueueCongestion.git "$NS3_DIR/src/dqc"
fi

# 3. Copy scratch programs
cp "$NS3_DIR/src/dqc/scratch/"*.cc "$NS3_DIR/scratch/"

# 4. DQC writes trace files here; must exist before any run
mkdir -p "$NS3_DIR/traces"

# 5. Persist env vars in a dedicated file so they don't leak into unrelated builds
cat > "$HOME/.dqcenv" <<EOF
# Sourced by experiments/dqc/*.sh
export NS3_DIR="$NS3_DIR"
export DQC="$DQC_INC"
export CPLUS_INCLUDE_PATH="\${CPLUS_INCLUDE_PATH:-}:\$DQC/include:\$DQC/congestion:\$DQC/logging"
EOF

# shellcheck disable=SC1091
source "$HOME/.dqcenv"

# 6. Configure + build (waf, NOT ./ns3)
cd "$NS3_DIR"
./waf configure --build-profile=optimized --disable-examples --disable-tests
./waf build

echo
echo "DQC build complete."
echo "NS3_DIR=$NS3_DIR"
echo "Source ~/.dqcenv in future shells to pick up include paths."
echo "Smoke test: (cd \$NS3_DIR && ./waf --run 'scratch/dqc-test --it=1 --cc=reno --lo=0')"
