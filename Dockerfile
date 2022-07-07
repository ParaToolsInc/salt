FROM ecpe4s/llvm-doe-e4s:2022-01-24

RUN DEBIAN_FRONTEND=noninteractive set -v && \
        apt-get update && \
        apt-get install -y --no-install-recommends ca-certificates curl vim default-jre wget openssh-server libz-dev m4 make locales git gcc g++ gfortran mpi-default-bin mpi-default-dev python-dev python3-dev && \
        rm -rf /var/lib/apt/lists/* && \
        localedef -i en_US -c -f UTF-8 -A /usr/share/locale/locale.alias en_US.UTF-8 && \
        useradd -ms /bin/bash salt

COPY configure.sh Dockerfile makefile.base README.md /tmp/salt/
COPY config_files /tmp/salt/config_files
COPY include /tmp/salt/include
COPY src /tmp/salt/src
COPY tests /tmp/salt/tests

WORKDIR /tmp/salt/
