# Stage 1. Check out LLVM source code and run the build.
FROM launcher.gcr.io/google/debian10:latest as builder
LABEL maintainer "ParaTools Inc."

# Install build dependencies of llvm.
# First, Update the apt's source list and include the sources of the packages.
RUN grep deb /etc/apt/sources.list | \
    sed 's/^deb/deb-src /g' >> /etc/apt/sources.list

# Install compiler, python and subversion.
RUN apt-get update && \
    apt-get install -y --no-install-recommends ca-certificates gnupg \
           build-essential cmake make python3 zlib1g wget subversion unzip git && \
    rm -rf /var/lib/apt/lists/*

# Install a newer ninja release. It seems the older version in the debian repos
# randomly crashes when compiling llvm.
RUN wget "https://github.com/ninja-build/ninja/releases/download/v1.8.2/ninja-linux.zip" && \
    echo "d2fea9ff33b3ef353161ed906f260d565ca55b8ca0568fa07b1d2cab90a84a07 ninja-linux.zip" \
        | sha256sum -c  && \
    unzip ninja-linux.zip -d /usr/local/bin && \
    rm ninja-linux.zip

# Clone LLVM repo
RUN git clone https://github.com/llvm/llvm-project.git && \
    cd llvm-project && git checkout release/14.x && \
    cd llvm && mkdir build

# CMake llvm build
RUN cmake -GNinja \
    -DCMAKE_INSTALL_PREFIX=/tmp/llvm \
    -DCMAKE_MAKE_PROGRAM=/usr/local/bin/ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DLLVM_ENABLE_PROJECTS=clang \
    -DLLVM_TARGETS_TO_BUILD=X86 \
    -S /llvm-project/llvm -B /llvm-project/llvm/build

# Build libraries, headers, and binaries
RUN cd /llvm-project/llvm/build && \
    ninja install-llvm-libraries install-llvm-headers install-llvm-tblgen \
        install-clang-libraries install-clang-headers install-clang \
        install-clang-resource-headers install-llvm-config install-cmake-exports

# Stage 2. Produce a minimal release image with build results.
FROM launcher.gcr.io/google/debian10:latest
LABEL maintainer "ParaTools Inc."

# Install packages for minimal useful image.
RUN apt-get update && \
    apt-get install -y --no-install-recommends libstdc++-7-dev \
        vim libz-dev libtinfo-dev make binutils cmake git && \
    rm -rf /var/lib/apt/lists/*

# Copy build results of stage 1 to /usr/local.
COPY --from=builder /tmp/llvm/ /usr/local/

# Copy local files to stage 2 image.
COPY configure.sh Dockerfile makefile.base README.md /tmp/salt/
COPY config_files /tmp/salt/config_files
COPY include /tmp/salt/include
COPY src /tmp/salt/src
COPY tests /tmp/salt/tests

WORKDIR /tmp/salt/
