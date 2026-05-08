#!/usr/bin/env bash
# Quick script to configure, build and test SALT-FM
# On Gilgamesh or other UO systems, you can load the SALT-FM stack/environment with:
# `source activate-salt-fm-env.sh`

set -o errexit
set -o nounset
set -o pipefail
set -o verbose

# Pick the C/C++ compiler. On macOS, default `clang`/`clang++` resolve to
# AppleClang under /usr/bin which lacks `flang`, so prefer the Homebrew
# LLVM keg when both `llvm` and `flang` formulae are installed. Honour
# CC/CXX from the environment if already set so direnv / module-loaded
# toolchains win.
CC_BIN="${CC:-clang}"
CXX_BIN="${CXX:-clang++}"
if [[ -z "${CC:-}" || -z "${CXX:-}" ]] && [[ "$(uname -s)" == "Darwin" ]] \
    && command -v brew >/dev/null 2>&1; then
  # `brew --prefix <formula>` returns the path even if the keg is not
  # installed, so probe the actual binaries to confirm presence.
  BREW_LLVM_PREFIX="$(brew --prefix llvm 2>/dev/null || true)"
  BREW_FLANG_PREFIX="$(brew --prefix flang 2>/dev/null || true)"
  if [[ -n "${BREW_LLVM_PREFIX}" && -x "${BREW_LLVM_PREFIX}/bin/clang" \
      && -n "${BREW_FLANG_PREFIX}" && -x "${BREW_FLANG_PREFIX}/bin/flang-new" ]]; then
    if [[ -z "${CC:-}" ]]; then
      CC_BIN="${BREW_LLVM_PREFIX}/bin/clang"
    fi
    if [[ -z "${CXX:-}" ]]; then
      CXX_BIN="${BREW_LLVM_PREFIX}/bin/clang++"
    fi
  else
    echo "warning: Homebrew detected but llvm and/or flang formulae are not installed;" >&2
    echo "         falling back to '${CC_BIN}' / '${CXX_BIN}'." >&2
    echo "         Install with: brew install llvm flang" >&2
  fi
fi

cmake -DCMAKE_C_COMPILER="${CC_BIN}" -DCMAKE_CXX_COMPILER="${CXX_BIN}" -Wdev -Wdeprecated -G Ninja -S . -B build
cmake --build build --parallel 8 --verbose || cmake --build build --verbose
# `--no-tests=error` causes ctest to fail rather than silently report
# success when zero tests are registered (e.g. configure picked a SALT
# variant without tests).
( cd build && ( ctest -j --output-on-failure --no-tests=error || ctest --rerun-failed --verbose --no-tests=error ) )
