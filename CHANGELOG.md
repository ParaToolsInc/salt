# Changelog

All notable changes to SALT are documented here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

- LLVM 20, 21, and 22 build compatibility while preserving LLVM 19 support,
  with the CI matrix expanded against `salt-dev:1.3` (LLVM 19), `:1.4`
  (LLVM 20), and `:1.5` (LLVM 21), and a `spack concretize` smoketest
  ([#45](https://github.com/ParaToolsInc/salt/pull/45) by @zbeekman):
  - [#34](https://github.com/ParaToolsInc/salt/issues/34) - Fortran build
    support for LLVM 20.
  - [#30](https://github.com/ParaToolsInc/salt/issues/30) - `-Werror`
    restricted to Debug builds so release configurations are not blocked
    by new compiler warnings.
- Selective Instrumentation File (SIF) matcher correctness and coverage
  ([#45](https://github.com/ParaToolsInc/salt/pull/45) by @zbeekman):
  - [#47](https://github.com/ParaToolsInc/salt/issues/47) - Fortran SIF
    matcher now requires a full-string match instead of a substring search,
    so routines and files with overlapping names are no longer incorrectly
    included or excluded.
  - [#29](https://github.com/ParaToolsInc/salt/issues/29) - added tests
    covering SIF routine whitelist/blacklist, file-level include/exclude,
    combined INCLUDE+EXCLUDE semantics, and glob handling.
  - Preserves `*` when converting SIF globs to regular expressions in the
    Flang plugin (regression caught while fixing #47).
- Test-suite resilience: TAU-dependent tests are skipped instead of failing
  the build when the required TAU Makefiles are missing
  ([#45](https://github.com/ParaToolsInc/salt/pull/45) by @zbeekman, closes
  [#44](https://github.com/ParaToolsInc/salt/issues/44) reported by
  @nchaimov).
- Regression test for Fortran 2008 submodule instrumentation, confirming
  that TAU calls land in submodule procedure bodies and that the parent
  module's bodyless interface declarations remain untouched
  ([#45](https://github.com/ParaToolsInc/salt/pull/45) by @zbeekman, closes
  [#33](https://github.com/ParaToolsInc/salt/issues/33)).
- Project version is now derived from a committed `.VERSION` file plus
  `git describe` build metadata, replacing hardcoded version constants
  that had drifted to `0.2` after v0.3.0 shipped. A vendored `.githooks/`
  directory (auto-installed by `cmake` configure) and a tag-push CI
  workflow guard against `.VERSION`/tag drift
  ([#45](https://github.com/ParaToolsInc/salt/pull/45) by @zbeekman).
- C++ instrumentation API now reads entries from the configuration file,
  with new scoped variants `main_insert_scope` and
  `function_begin_insert_scope`; minimum required CMake version raised to
  3.23.0 for `FILE_SET`
  ([#43](https://github.com/ParaToolsInc/salt/pull/43) by @nchaimov):
  - [#41](https://github.com/ParaToolsInc/salt/issues/41) - C++ API
    instrumentation now emits `TAU_PROFILE_SET_NODE`.

## [0.3.0] - 2025-02-04

- Fortran module files are handled correctly during instrumentation
  ([#38](https://github.com/ParaToolsInc/salt/pull/38) by @zbeekman).
- Timer names are correct for internal procedures inside Fortran main
  programs ([#37](https://github.com/ParaToolsInc/salt/pull/37) by
  @zbeekman).

## [0.2.0] - 2025-01-28

First public SALT-FM milestone: Fortran instrumentation via the Flang
plugin in addition to the existing C/C++ tooling.

- SALT-FM Fortran instrumentation: Flang plugin, `fparse-llvm` driver, and
  a `saltfm` wrapper script that selects the C/C++ or Fortran instrumentor
  based on source language. Includes selective instrumentation
  (`FILE_INCLUDE_LIST` / `FILE_EXCLUDE_LIST`, routine-level include and
  exclude), `#line` directives in instrumented Fortran output, and
  instrumentation for `if (...) return` statements
  ([#25](https://github.com/ParaToolsInc/salt/pull/25) by @zbeekman, with
  major Fortran-side work by @nchaimov).
- TAU is now optional when building SALT
  ([#25](https://github.com/ParaToolsInc/salt/pull/25) by @zbeekman).
- Funding acknowledgement section added to the README
  ([#27](https://github.com/ParaToolsInc/salt/pull/27) by @nchaimov).
- Project LICENSE ([#26](https://github.com/ParaToolsInc/salt/pull/26) by
  @zbeekman).

## Pre-tag history

Initial public development of the C/C++ instrumentor before any release
tag, ending with the v0.2.0 SALT-FM milestone.

- Refactor of TAU's instrumentor into a standalone repository with a
  frontend / instrumentor API
  ([#7](https://github.com/ParaToolsInc/salt/pull/7) by @vikram8128):
  - [#6](https://github.com/ParaToolsInc/salt/issues/6) - frontend-related
    functions split into a separate source file.
- Configurable instrumentation driven by a YAML configuration file,
  replacing hard-coded instrumentation
  ([#9](https://github.com/ParaToolsInc/salt/pull/9) by @PlatinumCD):
  - [#1](https://github.com/ParaToolsInc/salt/issues/1) - replaced
    hard-coded instrumentation with config-file-driven instrumentation.
- CMake-based build system replacing the previous custom configure script
  ([#10](https://github.com/ParaToolsInc/salt/pull/10) by @zbeekman):
  - [#4](https://github.com/ParaToolsInc/salt/issues/4) - switched build
    system to CMake.
- `--config_file` flag added to `cparse-llvm` so the configuration file
  can be supplied explicitly
  ([#12](https://github.com/ParaToolsInc/salt/pull/12) by @zbeekman):
  - [#11](https://github.com/ParaToolsInc/salt/issues/11) - config file
    can now be passed via flag instead of being implicit.
- `-DTAU_ROOT=<dir>` option for tests to point at an alternate TAU
  installation ([#14](https://github.com/ParaToolsInc/salt/pull/14) by
  @wspear).
- `TAU_PROFILE_SET_NODE` correctly emitted from `main` for C and C++
  instrumentation
  ([#22](https://github.com/ParaToolsInc/salt/pull/22) by @zbeekman):
  - [#21](https://github.com/ParaToolsInc/salt/issues/21) - C/C++ SALT now
    injects instrumentation correctly into `main`.
- Segfault fixed when the trailing `--` argument was omitted from
  `cparse-llvm` invocations
  ([#5](https://github.com/ParaToolsInc/salt/pull/5) by @vikram8128).
- Build fixed under Clang 15 with stricter pedantic diagnostics
  ([#13](https://github.com/ParaToolsInc/salt/pull/13) by @khuck).

[Unreleased]: https://github.com/ParaToolsInc/salt/compare/v0.3.0...HEAD
[0.3.0]: https://github.com/ParaToolsInc/salt/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/ParaToolsInc/salt/releases/tag/v0.2.0
