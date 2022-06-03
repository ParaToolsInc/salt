#!/bin/bash

llvm_path=$(spack location --install-dir llvm)
clang_path="$llvm_path/bin"
clang_inc_path="$llvm_path/include"
clang_lib_path="$llvm_path/lib"
clang_version=""

# First, check if the clang command is in the user's path
if ! command -v clang &> /dev/null
then
    echo "pdt-llvm: clang could not be found."
    echo "pdt-llvm: Please add llvm/clang to your path."
    #kill -INT $$
    echo "pdt-llvm: Otherwise, we'll assume you passed in --clang-dir=/path."
else
    # OK, we have clang, so extract some info
    clang_path=$(dirname $(which clang))
fi

arch_dir=$(./../../../utils/archfind)
prefix=`pwd`
install_prefix=`pwd`

usage() {
  echo "Usage: ./configure.sh [options]"
  echo ""
  echo "Options:"
  echo "    --clang-dir=<directory>      Sets path for clang executable. Default: result of 'which clang'."
  echo "    --clang-inc=<directory>      Sets path for clang libtooling includes."
  echo "    --clang-lib=<directory>      Sets path for clang libraries."
  echo "    --prefix=<directory>         Installation prefix path."
  echo "    --arch-dir=<arch>            Sets which directory make will place executables in, as expected by"
  echo "                                 TAU. If building for use on a different architecture, use archfind"
  echo "                                 (included with PDT) to set the correct architecture."
}

while [ $# -gt 0 ]; do
  case "$1" in
    --clang-dir=*)
      clang_path="${1#*=}"
      ;;
    --clang-inc=*)
      clang_inc_path="${1#*=}"
      ;;
    --clang-lib=*)
      clang_lib_path="${1#*=}"
      ;;
    --arch-dir=*)
      arch_dir="${1#*=}"
      ;;
    --prefix=*)
      install_prefix="${1#*=}"
      ;;
    -h|--help|-help)
      usage
      exit 0
      ;;
    *)
      printf "* ERROR: Invalid argument: %s\n" $1
      printf "* Run configure.sh --help to print usage.\n"
      exit 1
  esac
  shift
done

if [ "${clang_inc_path}" == "" ] ; then
    clang_inc_path=$(dirname $clang_path)/include
fi
if [ "${clang_lib_path}" == "" ] ; then
    clang_lib_path=$(dirname $clang_path)/lib
fi
clang_version=`${clang_path}/llvm-config --version`
llvm_libs=`${clang_path}/llvm-config --libs` 
llvm_libs="$llvm_libs -lrt -ldl -lpthread -lm -lz -ltinfo"
clang_libs="-lclang -lclangFrontendTool -lclangRewriteFrontend -lclangDynamicASTMatchers -lclangFormat -lclangTooling -lclangFrontend -lclangToolingCore -lclangASTMatchers -lclangCrossTU -lclangStaticAnalyzerCore -lclangStaticAnalyzerCheckers -lclangStaticAnalyzerFrontend -lclangARCMigrate -lclangParse -lclangDriver -lclangSerialization -lclangRewrite -lclangSema -lclangEdit -lclangIndex -lclangAnalysis -lclangAST -lclangLex -lclangBasic -lclangToolingInclusions -lclangCodeGen"

echo "pdt-llvm: Found clang in $clang_path"
echo "pdt-llvm: Setting clang include path to $clang_inc_path"
echo "pdt-llvm: Setting clang library path to $clang_lib_path"
echo "pdt-llvm: Setting clang version to $clang_version"
echo "pdt-llvm: Setting arch to $arch_dir"

cxx_std="-std=c++14"
# Make this more flexible
version_nine='^9\..*'
if [[ $clang_version =~ $version_nine ]] ; then
    cxx_std="-std=c++11"
fi
echo "Setting std to $cxx_std"

rm -f Makefile
touch Makefile
echo "CLANG_DIR=$clang_path" >> Makefile
echo "CLANG_INC_DIR=$clang_inc_path" >> Makefile
echo "CLANG_LIB_DIR=$clang_lib_path" >> Makefile
echo "ARCH=$arch_dir" >> Makefile
echo "PREFIX=$prefix" >> Makefile
echo "INSTALL_PREFIX=$install_prefix" >> Makefile
echo "CXX_STD=$cxx_std" >> Makefile
echo "LLVM_LIBS=$llvm_libs" >> Makefile
echo "CLANG_LIBS=$clang_libs" >> Makefile
echo "" >> Makefile
cat makefile.base >> Makefile

touch empty.c
${clang_path}/clang -E -v empty.c >& clang.out

# Get the path to clang
clang_path=$(dirname $(dirname $(readlink -f ${clang_path}/clang)))
# Make a regular expression containing the path to clang
#clang_path_expression="^${clang_path}/lib.*"
clang_path_expression="^.*/lib/clang/.*$"

# We are looking for a section of output that is like this:

# #include "..." search starts here:
# #include <...> search starts here:
#  /usr/include/python3.8
#  /usr/local/include
#  /home/khuck/spack/opt/spack/linux-ubuntu20.04-sandybridge/gcc-9.3.0/llvm-12.0.0-cczgwtbmt6532efbvue33ghvvigbbuuc/lib/clang/12.0.0/include
#  /usr/include/x86_64-linux-gnu
#  /usr/include
# End of search list.

headers=False
expression='^#include .+ search starts here:$'
headerstring=''
length=0
while read line; do
    line=`echo $line | xargs`
    if [[ $line =~ $expression ]] ; then
        headers=True
        continue
    fi
    if [[ $line == "End of search list." ]] ; then
        headers=False
        continue
    fi
    if [[ $headers == True ]] ; then
        # We only want the headers that are part of the clang installation
        if [[ $line =~ $clang_path_expression ]] ; then
            if [[ $headerstring == "" ]] ; then
                headerstring="\"-I$line\""
            else
                headerstring="${headerstring}, \"-I$line\""
            fi
            let "length=length+1"
        fi
    fi
done < clang.out

clang_header_includes="const char * const clang_header_includes[] = {"
echo "${clang_header_includes}${headerstring}};" > clang_header_includes.h
echo "const int clang_header_includes_length{${length}};" >> clang_header_includes.h
echo "Detected clang header paths: ${headerstring}"

rm empty.c clang.out

#cat clang_header_includes.h
