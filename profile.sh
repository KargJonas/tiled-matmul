#!/usr/bin/env bash

echo -e "Running perf ..."
perf stat -e cycles,instructions,task-clock,cpu-clock,context-switches,\
cache-misses,cache-references,branches,branch-misses,\
dTLB-load-misses,dTLB-loads,LLC-load-misses,LLC-loads \
  -o $(basename $1).profile -- "$@"
