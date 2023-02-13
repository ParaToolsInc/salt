[![CI](https://github.com/ParaToolsInc/salt/actions/workflows/CI.yaml/badge.svg)](https://github.com/ParaToolsInc/salt/actions/workflows/CI.yaml)

SALT: An LLVM-based Source Analysis Tookit for HPC
==================================================

The [TAU Performance System] is a powerful and versatile performance monitoring and analysis system.
TAU supports several different mechanisms for instrumentation of applications,
including source-based instrumentation.
Source-based instrumentation inserts instrumentation points around areas of interest
by parsing and modifying an application's source code.
However, source-based instrumentation can be difficult to use,
especially for large code-bases written in modern languages such as C++ and Fortran.
To improve the usability of TAU,
we introduce a next-generation source analysis toolkit called SALT.
SALT is built using LLVM compiler technology,
including the libTooling library, Clang's C and C++ parsers, and f18/Flang's Fortran parser.

## Getting Started

The build system for SALT utilizes [CMake] as the build system generator
(AKA meta build system).
It also requires either a full installation of LLVM and Clang including the
`clang-cmake-exports` and `cmake-exports`, or a more minimal set of features/components
(including both of the aformentioned `cmake-exports`) and a patch to be applied to
two of the LLVM/Clang installed CMake files.

The development and testing of SALT utilizes the [salt-dev] container,
which is [posted to Docker Hub].
The container image includes an installation of `git`, `cmake`, `llvm`/`clang` and `ninja`.
The installed `llvm` and `clang` include the patched [CMake] files.

Below are the steps required to get started using SALT:

### 1. Clone this repo:

``` shell
git clone --recursive https://github.com/ParaToolsInc/salt.git
```
Including the `--recursive` flag will ensure you have the patches
that are applied to the `llvm`/`clang` installed CMake files available to you.
This is not a requirement if using the [salt-dev] container image.
The patches prevent SALT's configuration with CMake from failing when
incomplete/minimal installations of LLVM and Clang are present.
A minimal subset of components must be installed.
If SALT is failing to be configured in the next step,
and you have write access to the LLVM and/or Clang installation,
applying the patches from https://github.com/ParaToolsInc/salt-llvm-patches
may fix the problem.

### 2. Fire up the salt-dev docker image (optional)

``` shell
$ docker pull paratools/salt-dev:latest
$ docker run -it --pull --tmpfs=/dev/shm:rw,nosuid,nodev,exec \
    --privileged -v $(pwd):/home/salt/src paratools/salt-dev:latest
# cd /home/salt/src
# ccache --show-stats
```
This step is optional but recommended.
The `llvm`/`clang` cmake files are patched and this is the environment used in CI testing.
The docker image also includes `ccache` and two configurations of TAU
for testing with both GCC and Clang.
(See `/usr/local/x86_64/lib` for the two installed `TAU_MAKEFILE`s.)

### 3. Configure and build SALT:

This step is most easily performed in the [salt-dev] container,
but can also be performed in a suitable installation of LLVM and Clang are present.

``` shell
# Tell CMake to use the clang/clang++ compiler to build SALT
export CC=clang
export CXX=clang++
# configure SALT and generate build system
cmake -Wdev -Wdeprecated -S . -B build
cmake --build build --parallel # Add --verbose to debug something going wrong
```

If the Ninja build system is present and you prefer it to Makefiles
(it's already present in the [salt-dev] container image),
then you may add `-G Ninja` to the penultimate line above:

``` shell
cmake -Wdev -Wdeprecated -S . -B build -G Ninja
```

Once this is finished you will have an executable `cparse-llvm` in the `build` directory.

### 4. Running the tests (optional):

Running the tests is incouraged to verify functionality.

``` shell
cd build
ctest --output-on-failure
```

The tests are all located in the `tests` subdirectory of the project.
[CMake] test fixtures and test dependencies ensure that:

 * The tests run in an order such that inter-test dependencies are satisfied
 * The SALT configurtion files are correctly staged to the build directory
 * Old instrumented versions of test sources are cleaned up JIT before being instrumented again
   * This is so the user can inspect instrumented sources
 * Old object files are cleaned up
 * Old profiles associated with each test are JIT removed
 * New profiles are moved to subdirectories indicating which test they are assosciated with

A limitation of the tests is that they assume a TAU installation matching the [salt-dev]
development image.
If you don't have a GCC and Clang configuration of TAU installed into the default location
(`/usr/local/x86_64/`) then some of the tests will fail.

### 5. Example usage:

```
# Point to the appropriate config file if config_files/config.yaml is not in CWD
./cparse-llvm --config_file=/path/to/config.yaml ../tests/hello.c
```

This will produce a file `hello.inst.c`.
Passing the correct defines (see the [CMakeLists.txt file]) and
setting `TAU_MAKEFILE=/usr/local/x86_64/lib/Makefile.tau-pthread` will allow you
to compile `hello.inst.c` with `tau_cc.sh -optLinkOnly -D... hello.inst.c -o hello`.
Running `tau_exec ./hello` should then produce a `profile.0.0.0` file.



[TAU Performance System]: http://www.tau.uoregon.edu/
[CMake]: https://cmake.org
[salt-dev]: https://github.com/ParaToolsInc/salt-dev
[posted to Docker Hub]: https://hub.docker.com/repository/docker/paratools/salt-dev/general
[CMakeLists.txt file]: https://github.com/ParaToolsInc/salt/blob/364be5ddd0043281669ace6697dfaf05fe724511/CMakeLists.txt#L353-L386
