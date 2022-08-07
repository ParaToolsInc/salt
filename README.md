SALT: An LLVM-based Source Analysis Tookit for HPC
==================================================

The TAU Performance System is a powerful and versatile performance monitoring and analysis system. TAU supports several different mechanisms for instrumentation of applications, including source-based instrumentation. Source-based instrumentation inserts instrumentation points around areas of interest by parsing and modifying an application's source code. However, source- based instrumentation can be difficult to use, especially for large code- bases written in modern languages such as C++ and Fortran. To improve the usability of TAU, we introduce a next-generation source analysis toolkit called SALT. SALT is built using LLVM compiler technology, including the libTooling library, Clang's C and C++ parsers, and f18/Flang's Fortran parser.

## Getting Started
Clone this repo:
```
git clone https://github.com/ParaToolsInc/salt.git
```

Configure and build:
```
cd salt
docker build . -t saltimage --no-cache
docker run -it --tmpfs=/dev/shm:rw,nosuid,nodev,exec --privileged -v $(pwd):/home/salt saltimage
./configure.sh
make
```

Example:
```
./cparse-llvm tests/hello.c 
```

