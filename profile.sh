#!/usr/bin/env bash
###############################################################################
# profile_matmul.sh – build, run, profile and diagnose the tiled matmul code
# Usage: ./profile_matmul.sh <m> <n> <p>
###############################################################################
set -euo pipefail
sizes=("$@")
if [ "${#sizes[@]}" -ne 3 ]; then
  echo "Usage: $0 <m> <n> <p>" ; exit 1
fi

# --------------------------------------------------------------------------- #
# 1. Build with aggressive optimisation and static analysis hints
# --------------------------------------------------------------------------- #
CFLAGS="-O3 -march=native -ffast-math -funroll-loops -flto \
        -fno-exceptions -fno-rtti -fstrict-aliasing \
        -falign-functions=64 -falign-loops=64 \
        -Wall -Wextra -Wshadow -Wconversion -Wpedantic"

echo "🛠️  Compiling main.parallel.c …"
gcc ${CFLAGS} -o main.parallel main.parallel.c -lpthread

# --------------------------------------------------------------------------- #
# 2. Run and capture perf counters
# --------------------------------------------------------------------------- #
echo -e "\n⏱️  Running perf – this can take a few seconds …"
perf stat -e cycles,instructions,task-clock,cpu-clock,context-switches,\
cache-misses,cache-references,branches,branch-misses,\
dTLB-load-misses,dTLB-loads,LLC-load-misses,LLC-loads \
  -o perf.raw -- \
  ./main.parallel "${sizes[@]}"

# --------------------------------------------------------------------------- #
# 3. Convert perf output to JSON that a Python helper can digest
# --------------------------------------------------------------------------- #
awk '
  /#/ { next }
  NF==4 { gsub(/,/, "", $1); print $1, $3 }
' perf.raw | python3 - "$@" <<'PY'
import sys, json, textwrap, subprocess, shutil, re, os, math
metrics = dict(iter(zip(sys.stdin.read().split()[::2],
                        map(int, sys.stdin.read().split()[1::2]))))

#######################################################################
# 4. Analyse – very coarse, but enough to flag the usual suspects
#######################################################################
nthreads   = os.cpu_count()
speedup_theoretical = nthreads
ipc        = metrics["instructions"] / metrics["cycles"]
llc_misses = metrics["LLC-load-misses"]
tlb_misses = metrics["dTLB-load-misses"]
csw        = metrics["context-switches"]

diag = []

if ipc < 1.5:
    diag.append("Low IPC ({:.2f}); the inner tile kernel is probably **not "
                "vectorising
