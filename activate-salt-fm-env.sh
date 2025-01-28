#!/usr/bin/env bash
# Sourec this file on UO machines to setup your SALT-FM development environment
# `source activate-salt-fm-env.sh`
# After this you can quickly configure, build, and test using `./build_and_test.sh`

export SALT_ROOT=/storage/packages/salt-fm
# export PATH="$SALT_ROOT/base/tools/bin:$PATH"
module use /packages/salt-fm/modules
module use /storage/packages/salt-fm/spack/share/spack/modules/linux-rhel8-x86_64
module use /storage/packages/salt-fm/spack/share/spack/modules/linux-centos7-x86_64/
echo "purging loaded modules"
module purge
echo "listing loaded modules:"
module list
echo "loading llvm, gcc and mpich:"
module load llvm/git.086d8e6bb5daf8de43880ba90258c49e0fabf2c9_19.1.4-zpacv56
module load mpich/4.2.3-ugxzfxf
module load gcc/14.2.0-ttkqi3s
echo "loading ninja"
module load ninja
#echo "loading tau"
module load /packages/salt-fm/modules/tau/2.34
echo "listing loaded modules:"
module list
echo "Finished"
