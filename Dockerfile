# syntax=docker/dockerfile:1.4
# Stage 1. Check out LLVM source code and run the build.
FROM launcher.gcr.io/google/debian11:latest as builder
LABEL maintainer "ParaTools Inc."

# Install build dependencies of llvm.
# First, Update the apt's source list and include the sources of the packages.
RUN grep deb /etc/apt/sources.list | \
    sed 's/^deb/deb-src /g' >> /etc/apt/sources.list

# Install compiler, cmake, git, ccache etc.
RUN apt-get update && \
    apt-get install -y --no-install-recommends ca-certificates \
           build-essential cmake ccache make python3 zlib1g wget unzip git && \
    rm -rf /var/lib/apt/lists/*

# Install a newer ninja release. It seems the older version in the debian repos
# randomly crashes when compiling llvm.
RUN wget "https://github.com/ninja-build/ninja/releases/download/v1.11.1/ninja-linux.zip" && \
    echo "b901ba96e486dce377f9a070ed4ef3f79deb45f4ffe2938f8e7ddc69cfb3df77 ninja-linux.zip" \
        | sha256sum -c  && \
    unzip ninja-linux.zip -d /usr/local/bin && \
    rm ninja-linux.zip

# Clone LLVM repo
RUN git clone --single-branch --depth=1 --branch=release/14.x --filter=blob:none https://github.com/llvm/llvm-project.git && \
    cd llvm-project/llvm && mkdir build

# use ccache (make it appear in path earlier then /usr/bin/gcc etc)
RUN for p in gcc g++ clang clang++ cc c++; do ln -vs /usr/bin/ccache /usr/local/bin/$p;  done

# CMake llvm build
RUN --mount=type=cache,target=/ccache/ cmake -GNinja \
    -DCMAKE_INSTALL_PREFIX=/tmp/llvm \
    -DCMAKE_MAKE_PROGRAM=/usr/local/bin/ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra" \
    -DLLVM_TARGETS_TO_BUILD=X86 \
    -S /llvm-project/llvm -B /llvm-project/llvm/build

RUN --mount=type=cache,target=/ccache/ ccache -s

# Build libraries, headers, and binaries
RUN --mount=type=cache,target=/ccache/ ccache -s nproc --all || lscpu || true && \
    cd /llvm-project/llvm/build && git branch && \
    ninja install-llvm-libraries install-llvm-headers \
        install-clang-libraries install-clang-headers install-clang install-clang-cmake-exports \
        install-clang-resource-headers install-llvm-config install-cmake-exports ; \
    find /tmp/llvm -name '*.cmake' -type f

RUN --mount=type=cache,target=/ccache/ ccache -s

# Patch installed cmake exports/config files to not throw an error if not all components are installed
RUN <<EOC
  patch --strip 1 --ignore-whitespace << 'EOF'
--- /tmp/llvm/lib/cmake/llvm/LLVMExports.cmake  2022-11-04 14:15:29.967057438 +0000
+++ /tmp/llvm/lib/cmake/llvm/LLVMExports.cmake.b        2022-11-04 13:55:14.935207352 +0000
@@ -825,7 +825,7 @@
 foreach(target ${_IMPORT_CHECK_TARGETS} )
   foreach(file ${_IMPORT_CHECK_FILES_FOR_${target}} )
     if(NOT EXISTS "${file}" )
-      message(FATAL_ERROR "The imported target \"${target}\" references the file
+      message(DEBUG "The imported target \"${target}\" references the file
    \"${file}\"
 but this file does not exist.  Possible reasons include:
 * The file was deleted, renamed, or moved to another location.
EOF
  patch --strip 1 --ignore-whitespace << 'EOF'
--- /tmp/llvm/lib/cmake/clang/ClangTargets.cmake        2022-11-04 14:48:01.627471510 +0000
+++ /tmp/llvm/lib/cmake/clang/ClangTargets.cmake.b      2022-11-04 14:57:45.345397332 +0000
@@ -710,7 +710,7 @@
 foreach(target ${_IMPORT_CHECK_TARGETS} )
   foreach(file ${_IMPORT_CHECK_FILES_FOR_${target}} )
     if(NOT EXISTS "${file}" )
-      message(FATAL_ERROR "The imported target \"${target}\" references the file
+      message(DEBUG "The imported target \"${target}\" references the file
    \"${file}\"
 but this file does not exist.  Possible reasons include:
 * The file was deleted, renamed, or moved to another location.
EOF
EOC

# Stage 2. Produce a minimal release image with build results.
FROM launcher.gcr.io/google/debian11:latest
LABEL maintainer "ParaTools Inc."

# Install packages for minimal useful image.
RUN apt-get update && \
    apt-get install -y --no-install-recommends libstdc++-10-dev \
        vim emacs-nox libz-dev libtinfo-dev make binutils cmake git && \
    rm -rf /var/lib/apt/lists/*

# Copy local files to stage 2 image.
COPY configure.sh Dockerfile makefile.base README.md /tmp/salt/
COPY config_files /tmp/salt/config_files
COPY include /tmp/salt/include
COPY src /tmp/salt/src
COPY tests /tmp/salt/tests

COPY --from=builder /usr/local/bin/ninja /usr/local/bin/

# Copy build results of stage 1 to /usr/local.
COPY --from=builder /tmp/llvm/ /usr/local/

WORKDIR /tmp/salt/
