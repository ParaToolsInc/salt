#!/usr/bin/env bash
# Quick script to configure, build and test SALT-FM
# On Gilgamesh or other UO systems, you can load the SALT-FM stack/environment with:
# `source activate-salt-fm-env.sh`

set -o errexit
set -o nounset
set -o pipefail
set -o verbose

cmake -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -Wdev -Wdeprecated -G Ninja -S . -B build
cmake --build build --parallel 8 --verbose || cmake --build build --verbose
( cd build && ctest --output-on-failure )
