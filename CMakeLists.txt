#--------------------------------
# Generic CMake settings for SALT
#--------------------------------

cmake_minimum_required(VERSION 3.23.0)
# Ensure policies are set as they have been tested
cmake_policy(VERSION 3.23.0...3.31.2)
if(POLICY CMP0144)
  cmake_policy(SET CMP0144 NEW)
endif()
if(POLICY CMP0074)
  cmake_policy(SET CMP0074 NEW)
endif()
if(POLICY CMP0042)
  # Use MACOSX_RPATH
  cmake_policy(SET CMP0042 NEW)
  set(CMAKE_MACOSX_RPATH ON)
endif()

# Big Sur is the oldest macOS supported by libc++ in LLVM 11+ (the floor
# we ship against). Targeting 10.15 emits "selected platform is no longer
# supported" warnings from <__configuration/availability.h>. The target
# may be raised again post-find_package(LLVM) to match the LLVM dylib's
# own LC_BUILD_VERSION minos.
if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")
endif()

# Auto-detect CMAKE_OSX_SYSROOT via xcrun if not set. Without it, CMake
# does not pass `-isysroot` and the active SDK's C headers come in via
# explicit `-isystem` entries (re-exported from LLVM's CMake config
# targets), which sit *higher* than libc++'s implicit search and break
# wrapper resolution for <cfloat> / <cmath> / <cinttypes> deep in our
# ryml_all.hpp / LLVM header chain. Must be set before `project()` so
# CMake's compiler probes and platform initialisation see it. xcrun
# ships with Xcode / Command Line Tools so it is always available on
# a configured macOS host.
if(APPLE AND NOT CMAKE_OSX_SYSROOT)
  execute_process(
    COMMAND xcrun --sdk macosx --show-sdk-path
    OUTPUT_VARIABLE _salt_xcrun_sdk
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _salt_xcrun_sdk_rc)
  if(_salt_xcrun_sdk_rc EQUAL 0 AND _salt_xcrun_sdk
      AND IS_DIRECTORY "${_salt_xcrun_sdk}")
    message(STATUS "Auto-detected CMAKE_OSX_SYSROOT: ${_salt_xcrun_sdk}")
    set(CMAKE_OSX_SYSROOT "${_salt_xcrun_sdk}" CACHE PATH
      "macOS SDK path (auto-detected via xcrun)")
  endif()
  unset(_salt_xcrun_sdk)
  unset(_salt_xcrun_sdk_rc)
endif()

# Test if being built under a multi-config generator, i.e. and IDE like MSVS
get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
# Organize MSVS projects with folders
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
# Look for our custom CMake Modules here: cmake/modules
list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_CURRENT_LIST_DIR}/cmake/modules")

# Specify _INIT variables to override platform/compiler defaults
# See https://cmake.org/cmake/help/v3.11/variable/CMAKE_USER_MAKE_RULES_OVERRIDE.html
# Platform/compiler/project specific flags are set in the USER_MAKE_RULES_OVERRIDE file
set(SALT_ALLOWED_BUILD_TYPES "Debug" "Release" "MinSizeRel" "RelWithDebInfo" "RuntimeDebug")

if(NOT isMultiConfig)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build configuration" FORCE)
  elseif(NOT CMAKE_BUILD_TYPE IN_LIST SALT_ALLOWED_BUILD_TYPES)
    message(FATAL_ERROR "Invalid build type: ${CMAKE_BUILD_TYPE}")
  endif()
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "${SALT_ALLOWED_BUILD_TYPES}")
endif()

# Only treat warnings as errors in Debug builds. Release builds tolerate new
# warnings introduced by newer compiler versions. Use a generator expression
# so multi-config generators (Visual Studio, Xcode, Ninja Multi-Config) honor
# the policy per-configuration -- a configure-time CMAKE_BUILD_TYPE check
# would be empty under those generators and silently drop -Werror.
set(SALT_WERROR "$<$<CONFIG:Debug>:-Werror>")

#------------------------------------------------------------
# SALT project specific global options, version, and defaults
#------------------------------------------------------------

# Ensure we're not building in the source tree root
include(CheckOutOfSourceBuild)
check_out_of_source_build()

# SALT project version
# Sources of truth (see cmake/modules/SALTVersion.cmake):
#   - .VERSION file (committed) -- canonical bare SemVer
#   - git describe (when .git/ present) -- adds build metadata for dev builds
include(SALTVersion)

# When configuring inside a git checkout, point the clone at the vendored
# .githooks/ directory so the version-drift pre-push hook is active.
include(SALTGitHooks)

# Print project logo, version and tag line
file(READ ${CMAKE_CURRENT_LIST_DIR}/cmake/SALT-logo.txt SALT_LOGO)
string(JOIN "\n         " SALT_GREETING "${SALT_LOGO}"
  "SALT-FM, v${SALT_VERSION_FULL}: An LLVM-based Source Analysis Toolkit for HPC")
if(NOT isMultiConfig)
  string(APPEND SALT_GREETING "\n         Build type: ${CMAKE_BUILD_TYPE}")
endif()
string(APPEND SALT_GREETING "\n")
string(PREPEND SALT_GREETING "\n")
message(STATUS "${SALT_GREETING}")

#---------------------------------------------------------------------------------
# Get & print diagnostics about flags passed to CMake and build/source directories
#---------------------------------------------------------------------------------
message(STATUS "CMake source directory: ${CMAKE_SOURCE_DIR}")
message(STATUS "CMake binary directory: ${CMAKE_BINARY_DIR}")
include(GetPassedCMakeArgs)
get_passed_cmake_args()
if(CMAKE_ARGS)
  message("Passed command line CMAKE_ARGS: ${CMAKE_ARGS}")
endif()

set(CMAKE_CXX_STANDARD 17)

#-----------------------------
# Create the main SALT project
#-----------------------------
project(saltfm
  VERSION "${SALT_VERSION_BASE}"
  DESCRIPTION "An LLVM-based Source Analysis Toolkit for HPC"
  HOMEPAGE_URL "https://github.com/ParaToolsInc/salt"
  LANGUAGES CXX C)

# Force libc++ for the Homebrew/Spack/source-built Clang we require on
# macOS. Without the explicit flag the libc++ header search order
# falls back to the active SDK's C headers before libc++'s wrappers,
# producing "<cfloat> tried including <float.h> but didn't find
# libc++'s <float.h>" errors deep in our ryml_all.hpp / LLVM header
# chain. (SALT requires Clang, not AppleClang, so the condition is
# narrow.) Skipped if the user already selected a stdlib via
# CMAKE_CXX_FLAGS or CXXFLAGS.
if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
    AND NOT CMAKE_CXX_FLAGS MATCHES "-stdlib=")
  string(APPEND CMAKE_CXX_FLAGS " -stdlib=libc++")
endif()

include(GNUInstallDirs)

#----------
# Find LLVM
#----------

# See https://llvm.org/docs/CMake.html#embedding-llvm-in-your-project for more
# information. If LLVM and/or Clang installations are partial, then their
# supplied CMake exports files need to be patched. This is already done in
# the salt-dev docker container. The patches are kept as a git submodule at
# ./cmake/llvm-patches and the upstream at
# https://github.com/ParaToolsInc/salt-llvm-patches. If either of the steps
# below fails and you have write access to the LLVM/Clang installations,
# try patching them using the supplied patches.

# Seed CMAKE_PREFIX_PATH from the active C++ compiler, llvm-config,
# and flang-new so find_package below locates non-default LLVM/Flang
# installs (Homebrew, Spack, conda, hand-built) without the user
# passing -DCMAKE_PREFIX_PATH. Toolchain-driven detection - no
# package-manager queries - so it works for any install whose bin/ is
# on PATH. Skipped entirely if CMAKE_PREFIX_PATH was already set by
# the user (cache or env).
function(_salt_seed_prefix tag path)
  if(NOT path OR "${path}" IN_LIST CMAKE_PREFIX_PATH)
    return()
  endif()
  message(STATUS "Seeding CMAKE_PREFIX_PATH from ${tag}: ${path}")
  list(APPEND CMAKE_PREFIX_PATH "${path}")
  set(CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}" PARENT_SCOPE)
endfunction()

if(NOT CMAKE_PREFIX_PATH AND "$ENV{CMAKE_PREFIX_PATH}" STREQUAL "")
  # `${CMAKE_CXX_COMPILER}` is the most reliable LLVM-prefix anchor:
  # if CMake's compiler probe just picked clang++ from a non-default
  # install, its <prefix>/bin/clang++ points straight at the LLVM
  # install root. Covers the common case where llvm-config was not
  # symlinked onto PATH (e.g. Homebrew keeps llvm-config in
  # /usr/local/opt/llvm/bin but only symlinks clang/clang++).
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
      OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    get_filename_component(_salt_cxx_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_salt_cxx_prefix "${_salt_cxx_bin}" DIRECTORY)
    _salt_seed_prefix("${CMAKE_CXX_COMPILER}" "${_salt_cxx_prefix}")
    unset(_salt_cxx_bin)
    unset(_salt_cxx_prefix)
  endif()
  find_program(SALT_LLVM_CONFIG_EXE NAMES llvm-config
    DOC "llvm-config used to seed CMAKE_PREFIX_PATH")
  if(SALT_LLVM_CONFIG_EXE)
    execute_process(
      COMMAND ${SALT_LLVM_CONFIG_EXE} --prefix
      OUTPUT_VARIABLE _salt_llvm_prefix
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE _salt_llvm_prefix_rc)
    if(_salt_llvm_prefix_rc EQUAL 0)
      _salt_seed_prefix("${SALT_LLVM_CONFIG_EXE}" "${_salt_llvm_prefix}")
    endif()
    unset(_salt_llvm_prefix)
    unset(_salt_llvm_prefix_rc)
  endif()
  # Homebrew ships flang in a separate keg from llvm; Spack also stages
  # them under distinct prefixes. Also probe for flang-new and add its
  # `<prefix>` so find_package(Flang CONFIG) can resolve.
  find_program(SALT_FLANG_NEW_EXE NAMES flang-new flang
    DOC "flang driver used to seed CMAKE_PREFIX_PATH")
  if(SALT_FLANG_NEW_EXE)
    get_filename_component(_salt_flang_bin "${SALT_FLANG_NEW_EXE}" DIRECTORY)
    get_filename_component(_salt_flang_prefix "${_salt_flang_bin}" DIRECTORY)
    _salt_seed_prefix("${SALT_FLANG_NEW_EXE}" "${_salt_flang_prefix}")
    unset(_salt_flang_bin)
    unset(_salt_flang_prefix)
  endif()
endif()

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

# On macOS, the LLVM dylib we link against often declares a higher
# LC_BUILD_VERSION minos than CMAKE_OSX_DEPLOYMENT_TARGET (Homebrew and
# Spack bottles target the build host's macOS). Linking that combination
# produces a swarm of "object file ... built for newer macOS version"
# warnings from ld and a portability promise we cannot honour anyway.
# Resolve the dylib via the compiler itself (-print-file-name) so this
# works for Homebrew, Spack, conda, or a hand-built LLVM. If the probe
# fails or the value is lower, the 11.0 floor stays in place.
if(APPLE)
  execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libLLVM.dylib
    OUTPUT_VARIABLE _salt_llvm_dylib
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _salt_pf_rc)
  if(_salt_pf_rc EQUAL 0
      AND _salt_llvm_dylib
      AND NOT _salt_llvm_dylib STREQUAL "libLLVM.dylib"
      AND EXISTS "${_salt_llvm_dylib}")
    execute_process(
      COMMAND otool -l "${_salt_llvm_dylib}"
      OUTPUT_VARIABLE _salt_otool_out
      RESULT_VARIABLE _salt_otool_rc)
    if(_salt_otool_rc EQUAL 0)
      string(REGEX MATCH "minos[ \t]+([0-9]+\\.[0-9]+)"
             _ "${_salt_otool_out}")
      if(CMAKE_MATCH_1
          AND CMAKE_MATCH_1 VERSION_GREATER
              "${CMAKE_OSX_DEPLOYMENT_TARGET}")
        message(STATUS
          "Raising CMAKE_OSX_DEPLOYMENT_TARGET "
          "${CMAKE_OSX_DEPLOYMENT_TARGET} -> ${CMAKE_MATCH_1} "
          "to match ${_salt_llvm_dylib}")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "${CMAKE_MATCH_1}")
      endif()
    endif()
  endif()
endif()

# get_cmake_property(_variableNames VARIABLES)
# list(SORT _variableNames)
# foreach(_variableName ${_variableNames})
#   message(STATUS "${_variableName}=${${_variableName}}")
# endforeach()

# Available LLVM components are stored in $LLVM_AVAILABLE_LIBS and can be found with
# `llvm-config --components`
# Set the LLVM components needed by salt, see tools in the llvm-project/clang/tools/ and
# llvm-project/clang-tools-extra for examples of CMakeLists.txt
set(LLVM_NEEDED_COMPONENTS
  option
  passes
)

# Find the libraries that correspond to the LLVM components
# that we wish to use
llvm_map_components_to_libnames(LLVM_LIBS ${LLVM_NEEDED_COMPONENTS})

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in:\n\t${LLVM_DIR}")
message(STATUS "LLVM include directory(s):\n\t${LLVM_INCLUDE_DIRS}")
message(STATUS "LLVM_LIBS (used by salt): ${LLVM_LIBS}")
message(STATUS "LLVM_DEFINITIONS: ${LLVM_DEFINITIONS}")
message(STATUS "LLVM_ENABLE_ASSERTIONS: ${LLVM_ENABLE_ASSERTIONS}")
message(STATUS "LLVM_ENABLE_EH: ${LLVM_ENABLE_EH}")
message(STATUS "LLVM_ENABLE_RTTI: ${LLVM_ENABLE_RTTI}")
if(NOT LLVM_ENABLE_RTTI)
  set(USE_RTTI -fno-rtti)
  message(STATUS
    "LLVM built with ENABLE_RTTI=FALSE, -fno-rtti will be passed when building & linking")
endif()
message(STATUS "LLVM_TOOLS_BINARY_DIR:\n\t${LLVM_TOOLS_BINARY_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})

list(LENGTH CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES NUM_CLANG_HEADER_INCLUDES)
list(JOIN CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES "\", \"-I" CLANG_HEADER_INCLUDES)
set(CLANG_HEADER_INCLUDES "\"-I${CLANG_HEADER_INCLUDES}\"")

# On macOS, libtooling needs -isysroot and the host clang's -resource-dir
# to satisfy libc++'s wrapped C headers (e.g. <cstddef> -> <stddef.h>).
# Without these, parsing user TUs fails with "tried including <stddef.h>
# but didn't find libc++'s <stddef.h>".
if(APPLE)
  execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} -print-resource-dir
    OUTPUT_VARIABLE SALT_CLANG_RESOURCE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _print_resource_dir_rc)
  if(_print_resource_dir_rc EQUAL 0
      AND SALT_CLANG_RESOURCE_DIR
      AND CMAKE_OSX_SYSROOT)
    string(APPEND CLANG_HEADER_INCLUDES
      ", \"-isysroot\", \"${CMAKE_OSX_SYSROOT}\""
      ", \"-resource-dir\", \"${SALT_CLANG_RESOURCE_DIR}\"")
    math(EXPR NUM_CLANG_HEADER_INCLUDES "${NUM_CLANG_HEADER_INCLUDES} + 4")
  endif()
endif()

message(STATUS "CLANG_HEADER_INCLUDES: ${CLANG_HEADER_INCLUDES}")
message(STATUS "NUM_CLANG_HEADER_INCLUDES: ${NUM_CLANG_HEADER_INCLUDES}")

# Fail-fast probe: the deepest external header in our include chain is
# the vendored ryml_all.hpp. On macOS a misconfigured C++ toolchain
# (e.g. Homebrew clang++ against the Command Line Tools SDK rather
# than the Xcode SDK, or libc++ wrapped C headers without a matching
# resource-dir) reliably blows up parsing this header from the actual
# build. Probe at configure time so misconfig surfaces here with a
# README pointer instead of as opaque header errors mid-build.
include(CheckCXXSourceCompiles)
set(_salt_ryml_save_includes "${CMAKE_REQUIRED_INCLUDES}")
set(_salt_ryml_save_flags "${CMAKE_REQUIRED_FLAGS}")
list(APPEND CMAKE_REQUIRED_INCLUDES "${CMAKE_SOURCE_DIR}/include")
list(APPEND CMAKE_REQUIRED_FLAGS "-std=c++17")
check_cxx_source_compiles("
#define RYML_SINGLE_HDR_DEFINE_NOW
#define RYML_SHARED
#include <ryml_all.hpp>
int main(void) { return 0; }
" SALT_RYML_HEADER_OK)
set(CMAKE_REQUIRED_INCLUDES "${_salt_ryml_save_includes}")
set(CMAKE_REQUIRED_FLAGS "${_salt_ryml_save_flags}")
unset(_salt_ryml_save_includes)
unset(_salt_ryml_save_flags)
if(NOT SALT_RYML_HEADER_OK)
  set(_salt_ryml_hint "")
  if(APPLE)
    set(_salt_ryml_hint
      " On macOS this typically means the chosen clang++ cannot resolve "
      "libc++ wrapped C headers against the active SDK. See the macOS "
      "section of README.md and verify the compiler selection first. "
      "If auto-detection does not work in your environment, you may need "
      "to override CMAKE_PREFIX_PATH, CMAKE_OSX_SYSROOT, or pass "
      "-stdlib=libc++ manually.")
  endif()
  message(FATAL_ERROR
    "Cannot compile <ryml_all.hpp> with the configured C++ toolchain."
    "${_salt_ryml_hint}")
endif()

#------------------------------------------
# Specify clang and system libraries needed
#------------------------------------------
# There should be a better way of doing this, but the llvm libtooling example just puts
# user written clang tools within the llvm/clang source tree....
set(CLANG_LIBS
  clangFrontend
  clangSerialization
  clangDriver
  clangTooling
  clangParse
  clangSema
  clangAnalysis
  clangEdit
  clangAST
  clangLex
  clangBasic
  clangRewrite
  clangRewriteFrontend
)

#-----------------------------------------
# Add an interface library to link against
#-----------------------------------------

# This provides an abstraction and shorthand so that you can just `target_link_libraries()` against
# the SALT_LLVM_TOOLING interface library
add_library(SALT_LLVM_TOOLING INTERFACE)
target_compile_features(SALT_LLVM_TOOLING INTERFACE cxx_std_17)
target_include_directories(SALT_LLVM_TOOLING INTERFACE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(SALT_LLVM_TOOLING INTERFACE ${LLVM_DEFINITIONS_LIST})
target_compile_options(SALT_LLVM_TOOLING INTERFACE
  ${USE_RTTI} -Wall -Wpedantic -Wno-unused-variable
  $<$<CONFIG:Debug>:-Wno-gnu-zero-variadic-macro-arguments>)
target_link_libraries(SALT_LLVM_TOOLING INTERFACE ${CLANG_LIBS} ${LLVM_LIBS})

#---------------------------------
# List the header and source files
#---------------------------------

# Explicitly listing source files is safer than globbing them--you might accidentally put
# an experiment or back up file in include or src and not realize it.
set(SALT_HEADER_FILES
  dprint.hpp
  ryml_all.hpp
  selectfile.hpp
  instrumentor.hpp
)

list(TRANSFORM SALT_HEADER_FILES PREPEND "${CMAKE_SOURCE_DIR}/include/")
foreach(header clang_header_includes.h frontend.hpp)
  configure_file(
    "${CMAKE_SOURCE_DIR}/include/${header}.in"
    "${CMAKE_BINARY_DIR}/include/${header}"
    @ONLY)
  list(APPEND SALT_HEADER_FILES "${CMAKE_BINARY_DIR}/include/${header}")
  set_source_files_properties(
    "${CMAKE_BINARY_DIR}/include/${header}"
    PROPERTIES
    GENERATED TRUE
  )
endforeach()

set(CPARSE_LLVM_SRCS
  frontend.cpp
  instrumentor.cpp
  selectfile.cpp
)

list(TRANSFORM CPARSE_LLVM_SRCS PREPEND "${CMAKE_SOURCE_DIR}/src/")

#---------------------
# Add the main targets
#---------------------
# If we refactor into a library that executables link against it will simplify
# this and reduce repetition
add_executable(cparse-llvm)
target_sources(cparse-llvm PUBLIC ${CPARSE_LLVM_SRCS})
target_sources(cparse-llvm
  PUBLIC
  FILE_SET headers
  TYPE HEADERS
  FILES ${SALT_HEADER_FILES}
  BASE_DIRS ${CMAKE_SOURCE_DIR}/include;${CMAKE_BINARY_DIR}/include)
target_include_directories(cparse-llvm PUBLIC
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_BINARY_DIR}/include")
target_compile_features(cparse-llvm PUBLIC cxx_std_17)
# Inherit definitions, compile features, etc.
target_link_libraries(cparse-llvm PUBLIC SALT_LLVM_TOOLING)
# You can try adding -static. The --as-needed/--no-allow-shlib-undefined/
# --no-undefined flags are GNU ld extensions; Apple ld doesn't recognize
# them, so gate on linker family.
target_link_options(cparse-llvm PUBLIC
  $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--as-needed>
  $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--no-allow-shlib-undefined>
  $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--no-undefined>)
# Turn on debug output if a debug build is being built
target_compile_definitions(cparse-llvm PUBLIC $<$<CONFIG:Debug>:DEBUG_NO_WAY>)
# Install the target
install(TARGETS cparse-llvm DESTINATION ${CMAKE_INSTALL_BINDIR})
set_target_properties(cparse-llvm PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}")

#------------------------------------------------------
# Handle config files in build directory & installation
#------------------------------------------------------
# Copy ${CMAKE_SOURCE_DIR}/config_files to
# ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/${CMAKE_PROJECT_NAME}/config_files
# and install them to ${CMAKE_INSTALL_DATADIR}/${CMAKE_PROJECT_NAME}/config_files
file(COPY ${CMAKE_SOURCE_DIR}/config_files
  DESTINATION ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/${CMAKE_PROJECT_NAME})
install(DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_DATADIR}/${CMAKE_PROJECT_NAME}
  DESTINATION ${CMAKE_INSTALL_DATADIR})

#---------------------
# Flang Frontend library
#---------------------

# Flang package requires MLIR
find_package(MLIR CONFIG)
if(MLIR_FOUND)
  message(STATUS "Found MLIR -- will check for Flang")
  find_package(Flang CONFIG)
else()
  message(STATUS "MLIR not found -- skipping Flang")
endif()

if(MLIR_FOUND AND Flang_FOUND)
  message(STATUS "Found Flang -- will build Flang frontend plugin")
  set(SALT_MOD_SUFFIX ".saltmod" CACHE STRING
    "Suffix for SALT-FM Fortran intrinsic .mod files")
  set(SALT_INC_DIR ${CMAKE_INSTALL_INCLUDEDIR}/salt/flang
    CACHE STRING "SALT include directory for Flang intrinsic modules")
  set(TEST_FORTRAN TRUE)

  # Find the llvm-flang directory containing the intrinsic modules so that we
  # can copy them and give them the SALT_MOD_SUFFIX
  list(LENGTH FLANG_INCLUDE_DIRS NUM_FLANG_INCLUDE_DIRS)
  message(DEBUG "NUM_FLANG_INCLUDE_DIRS: ${NUM_FLANG_INCLUDE_DIRS}")
  if(NUM_FLANG_INCLUDE_DIRS EQUAL 1)
    set(FLANG_INTRINSIC_MOD_DIR ${FLANG_INCLUDE_DIRS})
  else()
    set(FLANG_INTRINSIC_MOD_DIR ${FLANG_INSTALL_PREFIX})
  endif()
  message(DEBUG "FLANG_INTRINSIC_MOD_DIR: ${FLANG_INTRINSIC_MOD_DIR}")
  find_path(FLANG_INTRINSIC_MODS
    NAMES
      "ISO_Fortran_env.mod"
      "iso_fortran_env.mod"
      "ISO_fortran_env.mod"
      "iso_Fortran_env.mod"
    HINTS ${FLANG_INCLUDE_DIRS}
    PATH_SUFFIXES "flang" "${CMAKE_INSTALL_INCLUDEDIR}/flang"
    REQUIRED
  )
  message(STATUS "FLANG_INTRINSIC_MODS_DIR: ${FLANG_INTRINSIC_MODS}")

  # Get a list of intrinsic module files that we will need to copy and change
  # the extension for
  file(GLOB INTRINSIC_MOD_FILES
    LIST_DIRECTORIES false
    ${FLANG_INTRINSIC_MODS}/*.mod
  )
  message(STATUS "Found flang intrinsic module files: ${INTRINSIC_MOD_FILES}")

  # Variables set in FlangConfig.cmake
  message(STATUS "FLANG_CMAKE_DIR: ${FLANG_CMAKE_DIR}")
  message(STATUS "FLANG_EXPORTED_TARGETS: ${FLANG_EXPORTED_TARGETS}")
  message(STATUS "FLANG_INCLUDE_DIRS: ${FLANG_INCLUDE_DIRS}")

  # Interface for Flang frontend plugins
  add_library(SALT_FLANG_FRONTEND INTERFACE)
  target_compile_features(SALT_FLANG_FRONTEND INTERFACE cxx_std_17)
  target_include_directories(SALT_FLANG_FRONTEND INTERFACE
    ${LLVM_INCLUDE_DIRS} ${FLANG_INCLUDE_DIRS})
  target_compile_definitions(SALT_FLANG_FRONTEND INTERFACE ${LLVM_DEFINITIONS_LIST})
  target_compile_options(SALT_FLANG_FRONTEND INTERFACE
    ${USE_RTTI} -Wall -Wno-unused-variable -Wpedantic
    $<$<CONFIG:Debug>:-Werror>)

  # Endianness definitions are required, and Flang does not export a definitions list
  include(TestBigEndian)
  test_big_endian(IS_BIGENDIAN)
  if(IS_BIGENDIAN)
    target_compile_definitions(SALT_FLANG_FRONTEND INTERFACE FLANG_BIG_ENDIAN=1)
  else()
    target_compile_definitions(SALT_FLANG_FRONTEND INTERFACE FLANG_LITTLE_ENDIAN=1)
  endif()

  set(SALT_FLANG_PLUGIN_HEADER_FILES
    dprint.hpp
    selectfile.hpp
    flang_source_location.hpp
    flang_instrumentation_constants.hpp
    flang_instrumentation_point.hpp
  )
  list(TRANSFORM SALT_FLANG_PLUGIN_HEADER_FILES PREPEND "${CMAKE_SOURCE_DIR}/include/")

  set(SALT_FLANG_PLUGIN_SRCS
    dprint.cpp
    selectfile.cpp
    flang_source_location.cpp
    flang_instrumentation_point.cpp
    flang_salt_instrument_plugin.cpp
  )
  list(TRANSFORM SALT_FLANG_PLUGIN_SRCS PREPEND "${CMAKE_SOURCE_DIR}/src/")

  add_library(salt-flang-plugin SHARED)
  target_sources(salt-flang-plugin PUBLIC ${SALT_FLANG_PLUGIN_SRCS})
  target_sources(salt-flang-plugin
    PUBLIC
    FILE_SET headers
    TYPE HEADERS
    FILES ${SALT_FLANG_PLUGIN_HEADER_FILES})
  target_include_directories(salt-flang-plugin PUBLIC
    "${CMAKE_SOURCE_DIR}/include" "${CMAKE_BINARY_DIR}/include")
  target_compile_features(salt-flang-plugin PUBLIC cxx_std_17)
  target_link_libraries(salt-flang-plugin PUBLIC SALT_FLANG_FRONTEND)
  # Apple ld uses `-undefined dynamic_lookup`; GNU ld uses --as-needed and
  # rejects -undefined dynamic_lookup. Pick per linker.
  target_link_options(salt-flang-plugin PUBLIC
    $<$<NOT:$<PLATFORM_ID:Darwin>>:-Wl,--as-needed>
    $<$<PLATFORM_ID:Darwin>:-Wl,-undefined>
    $<$<PLATFORM_ID:Darwin>:-Wl,dynamic_lookup>)
  set_target_properties(salt-flang-plugin PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}")

  install(TARGETS salt-flang-plugin DESTINATION ${CMAKE_INSTALL_LIBDIR})

  configure_file(
    ${CMAKE_SOURCE_DIR}/src/fparse-llvm.in
    ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/fparse-llvm
    @ONLY)
  install(PROGRAMS ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/fparse-llvm
    TYPE BIN) # TYPE BIN installs into CMAKE_INSTALL_BINDIR

  # Copy the flang intrinsic module files into the salt include directory in
  # the build tree and change the file extension to SALT_MOD_SUFFIX
  foreach(intrinsic_mod_file IN LISTS INTRINSIC_MOD_FILES)
    get_filename_component(INTRINSIC_MOD_BASE ${intrinsic_mod_file} NAME_WLE)
    set(SALT_INTRINSIC_MOD_FILE
      ${CMAKE_BINARY_DIR}/${SALT_INC_DIR}/${INTRINSIC_MOD_BASE}${SALT_MOD_SUFFIX})
    message(STATUS "Copying ${intrinsic_mod_file} to ${SALT_INTRINSIC_MOD_FILE}")
    file(COPY ${intrinsic_mod_file} DESTINATION ${CMAKE_BINARY_DIR}/${SALT_INC_DIR})
    file(RENAME
      ${CMAKE_BINARY_DIR}/${SALT_INC_DIR}/${INTRINSIC_MOD_BASE}.mod
      ${SALT_INTRINSIC_MOD_FILE})
  endforeach()

  # Install the flang intrinsic module files into the salt include directory
  install(DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_INCLUDEDIR}/salt
    TYPE INCLUDE)

else()
  message(STATUS "Flang not found -- skipping Flang frontend plugin")
endif()

configure_file(
  ${CMAKE_SOURCE_DIR}/src/saltfm.in
  ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/saltfm
  @ONLY)
install(PROGRAMS ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/saltfm
  TYPE BIN) # TYPE BIN installs into CMAKE_INSTALL_BINDIR

#---------------------
# Find TAU locations for testing
#---------------------

# Resolve TAU_ROOT in priority order: explicit -DTAU_ROOT cache var,
# then $TAU_ROOT environment, then auto-detected from `tau_exec` on PATH.
# All three paths land in the same cache var so subsequent configures
# and cache inspectors see one consistent value.
set(TAU_PATH_SUFFIXES x86_64 x86_64/bin craycnl craycnl/bin apple apple/bin)
if(NOT TAU_ROOT AND DEFINED ENV{TAU_ROOT})
  set(TAU_ROOT "$ENV{TAU_ROOT}" CACHE PATH "TAU Root Directory")
endif()
if(NOT TAU_ROOT)
  find_program(TAU_EXEC tau_exec
    PATH_SUFFIXES ${TAU_PATH_SUFFIXES}
  )
  if(NOT TAU_EXEC)
    message(STATUS "TAU not found. Please set TAU_ROOT to the TAU installation directory.")
  else()
    get_filename_component(_tau_root ${TAU_EXEC} DIRECTORY) # bin directory
    get_filename_component(_tau_root ${_tau_root} DIRECTORY) # arch directory
    string(REGEX REPLACE "(/x86_64$)|(/apple$)|(/craycnl$)" ""
      _tau_root "${_tau_root}")
    set(TAU_ROOT "${_tau_root}" CACHE PATH "TAU Root Directory")
    unset(_tau_root)
  endif()
endif()

# Find the TAU makefiles, executables, scripts, libraries, etc.
message(STATUS "TAU_ROOT: ${TAU_ROOT}")

find_program(TAU_EXEC tau_exec
  PATHS ${TAU_ROOT}/x86_64 ${TAU_ROOT}/apple ${TAU_ROOT}/craycnl
  PATH_SUFFIXES bin
)
# Hardening switch: when ON, configure fails if TAU is present but any
# required artifact is missing. Default OFF so end-user installs degrade
# gracefully (HAVE_TAU=FALSE skips TAU-dependent tests). CI sets this ON.
option(SALT_REQUIRE_TAU
  "Fail configure if TAU is present but any required artifact is missing"
  OFF)

if(TAU_EXEC)
  get_filename_component(TAU_ARCH_DIR ${TAU_EXEC} DIRECTORY)
  get_filename_component(TAU_ARCH_DIR ${TAU_ARCH_DIR} DIRECTORY)
  message(STATUS "TAU_ARCH_DIR: ${TAU_ARCH_DIR}")
  # tau_exec / tau_cc.sh / tau_f90.sh internally call sibling helpers
  # (tau-config, tau_compiler.sh) by bare name, requiring TAU's bin
  # directory on PATH. Distros that install TAU into /usr/local pick this
  # up implicitly; in-tree installs (common on macOS) do not. Tests below
  # prepend ${TAU_BIN_DIR} via ENVIRONMENT_MODIFICATION.
  set(TAU_BIN_DIR "${TAU_ARCH_DIR}/bin")

  find_file(TAU_LLVM_MAKEFILE
    NAMES Makefile.tau-clang-pthread Makefile.tau-clang-pthread-pdt
    PATHS ${TAU_ARCH_DIR} PATH_SUFFIXES lib
  )
  find_file(TAU_GCC_MAKEFILE
    NAMES Makefile.tau-pthread Makefile.tau-pthread-pdt
    PATHS ${TAU_ARCH_DIR} PATH_SUFFIXES lib
  )
  find_program(TAUCC tau_cc.sh
    PATHS ${TAU_ARCH_DIR} PATH_SUFFIXES bin
  )
  find_program(TAUCXX tau_cxx.sh
    PATHS ${TAU_ARCH_DIR} PATH_SUFFIXES bin
  )
  find_program(TAUF90 tau_f90.sh
    PATHS ${TAU_ARCH_DIR} PATH_SUFFIXES bin
  )

  set(_salt_tau_missing "")
  foreach(tau_var TAU_LLVM_MAKEFILE TAU_GCC_MAKEFILE TAUCC TAUCXX TAUF90)
    if(NOT ${tau_var})
      list(APPEND _salt_tau_missing ${tau_var})
    endif()
  endforeach()
  if(_salt_tau_missing)
    set(HAVE_TAU FALSE)
    set(_msg
      "TAU found at ${TAU_ROOT} but missing required artifacts: "
      "${_salt_tau_missing}. TAU-dependent tests will be skipped.")
    if(SALT_REQUIRE_TAU)
      message(FATAL_ERROR "${_msg} (SALT_REQUIRE_TAU=ON)")
    else()
      message(WARNING "${_msg}")
    endif()
  else()
    set(HAVE_TAU TRUE)
  endif()
  unset(_salt_tau_missing)
else()
  set(HAVE_TAU FALSE)
  if(SALT_REQUIRE_TAU)
    message(FATAL_ERROR "SALT_REQUIRE_TAU=ON but tau_exec not found under ${TAU_ROOT}")
  endif()
endif()

# Derive the tau_exec `-T` token list from a Makefile.tau-<variant> path.
# `tau-config --binding` is filename-driven: with two installed variants
# (e.g. `Makefile.tau-pthread` and `Makefile.tau-clang-pthread`) plain
# `-T serial,pthread` matches both and the shortest-binding tiebreaker
# silently picks the gcc-stack variant. On macOS that mismatches the
# clang-stack TAU statically linked into the test binary. Encoding every
# `-` separated token from the variant name into the `-T` list narrows
# the match to a single binding directory regardless of toolchain.
function(_salt_tau_exec_tags out_var tau_makefile)
  get_filename_component(_mk "${tau_makefile}" NAME)
  string(REGEX REPLACE "^Makefile\\.tau-" "" _tags "${_mk}")
  string(REPLACE "-" "," _tags "${_tags}")
  set(${out_var} "serial,${_tags}" PARENT_SCOPE)
endfunction()

#---------------
# Tests
#---------------
# There are some assumptions made in testing:
# 1. Test source files to be instrumented are located in ${CMAKE_SOURCE_DIR}/tests/
# 2. Tests are currently not using MPI
# 3. Only pthread threading is assumed
# 4. TAU is installed with the following configuratins:
#    - Makefile.tau-clang-pthread
#    - Makefile.tau-pthread-pdt

# Care has been taken to break test inter-dependencies where possible and
# express them explicitly with the DEPENDS test property.
# Also each test should not enter a race condition or need to use the
# RESOURCE_LOCK property.
# e.g., Executable names are different when compiled with gcc vs clang and
# each test using its own PROFILE_DIR so as not to clobber profile.n.n.n files.
# As a result you may run the tests in parallel via something like:
# `ctest --output-on-failure -j 20`

# List of tests to run from ${CMAKE_SOURCE_DIR}/tests/
# When adding more test sources, add them here.
set(TESTS_LIST
  hello.c
  1d.c
  c11test.cpp
  forward_decl.c
  pure_virtual.cpp
)

include(CTest)

# Test label helpers. Labels are grouped by key:value so "Label Time
# Summary" buckets meaningfully and `ctest -L "^lang:"` (etc.) filters
# by dimension.
#   lang:{C,CXX,Fortran}
#   toolchain:{GCC,LLVM}
#   phase:{smoke,instrument,compile,run,check,setup}
function(_salt_lang_to_label lang_in out_var)
  string(TOLOWER "${lang_in}" _l)
  if(_l STREQUAL "c")
    set(${out_var} "lang:C" PARENT_SCOPE)
  elseif(_l STREQUAL "cpp" OR _l STREQUAL "cxx" OR _l STREQUAL "c++")
    set(${out_var} "lang:CXX" PARENT_SCOPE)
  elseif(_l STREQUAL "fortran" OR _l MATCHES "^f(77|90|03|08)?$")
    set(${out_var} "lang:Fortran" PARENT_SCOPE)
  else()
    set(${out_var} "lang:unknown" PARENT_SCOPE)
  endif()
endfunction()

# Derive a `lang:<L>` ctest label from a source filename's extension by
# stripping the leading dot and delegating to _salt_lang_to_label.
function(_salt_ext_to_label src out_var)
  get_filename_component(_ext ${src} LAST_EXT)
  string(REGEX REPLACE "^\\." "" _ext "${_ext}")
  _salt_lang_to_label("${_ext}" _label)
  set(${out_var} ${_label} PARENT_SCOPE)
endfunction()

# Map a compiler driver name (gcc, clang, gfortran, flang-new, ...) to a
# `toolchain:<vendor>` ctest label. GCC family -> GCC; Clang/Flang -> LLVM.
function(_salt_compiler_to_toolchain comp out_var)
  string(TOLOWER "${comp}" _c)
  # Anchor to full-string match so e.g. `clang++` does not match the
  # `g\+\+` alternative; allow an optional `-<version>` suffix
  # (gcc-13, clang-17, 17.0.4, ...).
  if(_c MATCHES "^(gcc|g\\+\\+|gfortran)(-[0-9]+(\\.[0-9]+)*)?$")
    set(${out_var} "toolchain:GCC" PARENT_SCOPE)
  elseif(_c MATCHES "^(clang(\\+\\+)?|flang(-new)?)(-[0-9]+(\\.[0-9]+)*)?$")
    set(${out_var} "toolchain:LLVM" PARENT_SCOPE)
  else()
    set(${out_var} "toolchain:${comp}" PARENT_SCOPE)
  endif()
endfunction()

# Map a compiler driver name to its toolchain build subdir (GCC or LLVM).
# Same vendor classification as _salt_compiler_to_toolchain, but bare name
# (no `toolchain:` prefix) suitable for use as a CMAKE_BINARY_DIR subdir
# and as the inflection in TAU_<dir>_MAKEFILE variable lookups.
function(_salt_compiler_to_dir comp out_var)
  string(TOLOWER "${comp}" _c)
  # Same vendor classification as _salt_compiler_to_toolchain; see that
  # function for the regex rationale (full-string anchor + optional
  # `-<version>` suffix).
  if(_c MATCHES "^(gcc|g\\+\\+|gfortran)(-[0-9]+(\\.[0-9]+)*)?$")
    set(${out_var} "GCC" PARENT_SCOPE)
  elseif(_c MATCHES "^(clang(\\+\\+)?|flang(-new)?)(-[0-9]+(\\.[0-9]+)*)?$")
    set(${out_var} "LLVM" PARENT_SCOPE)
  else()
    string(TOUPPER "${comp}" _u)
    set(${out_var} "${_u}" PARENT_SCOPE)
  endif()
endfunction()

# Smoke test to check instrumentor compiled and will run
add_test(NAME salt_parser_smoke_test
  COMMAND $<TARGET_FILE:cparse-llvm> --help)
set_tests_properties(salt_parser_smoke_test
  PROPERTIES
  LABELS "phase:smoke"
  PASS_REGULAR_EXPRESSION "OVERVIEW"
)

# Map an entry from TESTS_LIST to a saltfm parser test (with a follow-up
# check that the .inst file was emitted). Optional second argument overrides
# the derived test name; passing it is somewhat fragile and likely more
# trouble than it's worth.
function(add_instrumentor_test test_src)
  get_filename_component(test_base_name ${test_src} NAME_WE)
  set(test_name "instrument_${test_base_name}")
  get_filename_component(test_lang ${test_src} LAST_EXT)
  set(extra_args ${ARGN})
  list(LENGTH extra_args n_extra_args)
  if(n_extra_args GREATER 1)
    message(AUTHOR_WARNING
      "Incorrect number of arguments (1 + ${n_extra_args}) passed to add_instrumentor_test!")
    return()
  endif()
  if(n_extra_args EQUAL 1)
    set(test_name ${ARGV1})
  endif()
  _salt_ext_to_label(${test_src} _lang_label)
  add_test(NAME ${test_name}
    COMMAND
      ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/saltfm
      ${CMAKE_SOURCE_DIR}/tests/${test_src})
  set_tests_properties(${test_name}
    PROPERTIES
    REQUIRED_FILES "${CMAKE_SOURCE_DIR}/tests/${test_src}"
    LABELS "${_lang_label};phase:instrument"
    PASS_REGULAR_EXPRESSION "[Ii]nstrumentation:"
  )
  add_test(NAME ${test_name}_exists
    COMMAND ${CMAKE_COMMAND} -E cat ./${test_base_name}.inst${test_lang}
  )
  set_tests_properties(${test_name}_exists
    PROPERTIES
    DEPENDS ${test_name}
    LABELS "${_lang_label};phase:check"
    PASS_REGULAR_EXPRESSION "TAU_PROFILE_SET_NODE"
  )
endfunction()

foreach(test_source IN LISTS TESTS_LIST)
  add_instrumentor_test(${test_source})
endforeach()

# Issue #53 regression: bodyless FunctionDecls must be skipped, not
# instrumented or crashed-on. Each check_<name>_skips_bodyless test
# reads the .inst output and verifies (a) main was instrumented
# (PASS_REGULAR_EXPRESSION) and (b) no TAU_PROFILE_TIMER mentions
# the symbols whose getBody() returns nullptr (FAIL_REGULAR_EXPRESSION).
# Symbol names are deliberately distinct from any text used in the
# fixture's comment block so the FAIL regex cannot false-positive on
# documentation strings.
add_test(NAME check_forward_decl_skips_bodyless
  COMMAND ${CMAKE_COMMAND} -E cat forward_decl.inst.c)
set_tests_properties(check_forward_decl_skips_bodyless
  PROPERTIES
  DEPENDS instrument_forward_decl_exists
  LABELS "lang:C;phase:check"
  PASS_REGULAR_EXPRESSION "TAU_PROFILE_SET_NODE"
  FAIL_REGULAR_EXPRESSION "TAU_PROFILE_TIMER[^\n]*undefined_in_tu"
)

add_test(NAME check_pure_virtual_skips_bodyless
  COMMAND ${CMAKE_COMMAND} -E cat pure_virtual.inst.cpp)
set_tests_properties(check_pure_virtual_skips_bodyless
  PROPERTIES
  DEPENDS instrument_pure_virtual_exists
  LABELS "lang:CXX;phase:check"
  PASS_REGULAR_EXPRESSION "TAU_PROFILE_SET_NODE"
  FAIL_REGULAR_EXPRESSION
    "TAU_PROFILE_TIMER[^\n]*Abstract::unimplemented;TAU_PROFILE_TIMER[^\n]*undefined_free_fn"
)

# Selective Instrumentation File (SIF) tests.
#
# Each test runs the SALT instrumentor against a small source file containing
# two routines (keep_routine and excluded_routine) under a SIF that exercises
# one of the supported list types, then inspects the instrumented output for
# (or absence of) TAU_PROFILE_TIMER markers.
#
# Timer string format markers:
#   C/C++: "<return-type> <name>(<params>)  [{<file>}..."
#   Fortran (continued line): "&<name> [{<file>}..."
#
# Each test passes --tau_output so parallel ctest invocations don't clobber a
# shared default output path (saltfm writes to <basename>.inst.<ext> by default).

# Standard SIF test: instrumented output IS produced and inspected by
# check_sif_<test_name>. The caller is responsible for setting
# PASS_REGULAR_EXPRESSION / FAIL_REGULAR_EXPRESSION on the check test.
function(add_sif_test test_name lang src sif)
  set(name sif_${test_name})
  get_filename_component(_src_ext ${src} EXT)
  set(out_file ${name}.inst${_src_ext})
  _salt_lang_to_label(${lang} _lang_label)
  add_test(NAME instrument_${name}
    COMMAND ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/saltfm
            --tau_select_file=${CMAKE_SOURCE_DIR}/${sif}
            --tau_output=${out_file}
            ${CMAKE_SOURCE_DIR}/${src}
  )
  if(lang STREQUAL "fortran")
    set(instrument_pass_pattern "SALT Instrumentor Plugin finished")
  else()
    set(instrument_pass_pattern "[Ii]nstrumentation:")
  endif()
  set_tests_properties(instrument_${name}
    PROPERTIES
    REQUIRED_FILES "${CMAKE_SOURCE_DIR}/${src};${CMAKE_SOURCE_DIR}/${sif}"
    ENVIRONMENT "SALT_FORTRAN_VERBOSE=1"
    LABELS "${_lang_label};phase:instrument;sif"
    PASS_REGULAR_EXPRESSION "${instrument_pass_pattern}"
  )
  add_test(NAME check_${name}
    COMMAND ${CMAKE_COMMAND} -E cat ./${out_file}
  )
  set_tests_properties(check_${name}
    PROPERTIES
    DEPENDS instrument_${name}
    LABELS "${_lang_label};phase:check;sif"
  )
endfunction()

# SIF test where the instrumentor is expected NOT to emit instrumented code
# (e.g. the source is on a FILE_EXCLUDE_LIST or fails the FILE_INCLUDE_LIST
# whitelist). Verified by the absence of cparse-llvm's "Instrumentation:"
# banner. Pass --tau_output to a per-test unique path so parallel ctest runs
# cannot race on a shared default location.
function(add_sif_test_no_output test_name lang src sif)
  set(name sif_${test_name})
  get_filename_component(_src_ext ${src} EXT)
  set(out_file ${name}.inst${_src_ext})
  _salt_lang_to_label(${lang} _lang_label)
  add_test(NAME instrument_${name}
    COMMAND ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/saltfm
            --tau_select_file=${CMAKE_SOURCE_DIR}/${sif}
            --tau_output=${out_file}
            ${CMAKE_SOURCE_DIR}/${src}
  )
  set_tests_properties(instrument_${name}
    PROPERTIES
    REQUIRED_FILES "${CMAKE_SOURCE_DIR}/${src};${CMAKE_SOURCE_DIR}/${sif}"
    LABELS "${_lang_label};phase:instrument;sif"
    FAIL_REGULAR_EXPRESSION "Instrumentation:"
  )
endfunction()

# Pattern helpers: regex fragments that match a TAU_PROFILE_TIMER timer string
# referencing a named routine. The C/C++ matcher emits the routine signature
# inline; the Fortran matcher emits a continuation line with the bare name.
if(TEST_FORTRAN)
  set(_sif_keep_pattern_fortran    "keep_routine \\[\\{")
  set(_sif_excluded_pattern_fortran "excluded_routine \\[\\{")
endif()
set(_sif_keep_pattern_c     "keep_routine[(]void[)] +\\[\\{")
set(_sif_keep_pattern_cpp   "${_sif_keep_pattern_c}")
set(_sif_excluded_pattern_c "excluded_routine[(]void[)] +\\[\\{")
set(_sif_excluded_pattern_cpp "${_sif_excluded_pattern_c}")

# Routine-level EXCLUDE_LIST: blacklist excluded_routine.
add_sif_test(exclusion_c   c   tests/sif_excl.c   tests/sif/exclude_c.tau)
add_sif_test(exclusion_cpp cpp tests/sif_excl.cpp tests/sif/exclude_cpp.tau)
foreach(lang c cpp)
  set_tests_properties(check_sif_exclusion_${lang}
    PROPERTIES
    PASS_REGULAR_EXPRESSION "${_sif_keep_pattern_${lang}}"
    FAIL_REGULAR_EXPRESSION "${_sif_excluded_pattern_${lang}}"
  )
endforeach()

# File-level EXCLUDE_LIST: the C/C++ frontend short-circuits and emits no
# .inst output at all when the source file is on the file exclude list.
# Verified via absence of the "Instrumentation:" banner from cparse-llvm.
add_sif_test_no_output(file_exclude_c   c   tests/sif_excl.c   tests/sif/file_exclude_c.tau)
add_sif_test_no_output(file_exclude_cpp cpp tests/sif_excl.cpp tests/sif/file_exclude_cpp.tau)

# FILE_INCLUDE_LIST: cover both branches of the whitelist gate.
# - match: source IS on the list, so instrumentation proceeds and
#   keep_routine appears in the output.
# - nomatch: list contains a real but unrelated file, so the source under
#   test is not whitelisted and no .inst output is emitted (C/C++).
# Together these protect against both "list silently ignored" and
# "match condition silently over-broad" parser regressions.
add_sif_test(file_include_match_c
  c   tests/sif_excl.c   tests/sif/file_include_match_c.tau)
add_sif_test(file_include_match_cpp
  cpp tests/sif_excl.cpp tests/sif/file_include_match_cpp.tau)
foreach(lang c cpp)
  set_tests_properties(check_sif_file_include_match_${lang}
    PROPERTIES
    PASS_REGULAR_EXPRESSION "${_sif_keep_pattern_${lang}}"
  )
endforeach()
add_sif_test_no_output(file_include_nomatch_c
  c   tests/sif_excl.c   tests/sif/file_include_nomatch_c.tau)
add_sif_test_no_output(file_include_nomatch_cpp
  cpp tests/sif_excl.cpp tests/sif/file_include_nomatch_cpp.tau)

if(TEST_FORTRAN)
  add_sif_test(exclusion_fortran
    fortran tests/fortran/sif_excl.f90 tests/sif/exclude_f90.tau)
  set_tests_properties(check_sif_exclusion_fortran
    PROPERTIES
    PASS_REGULAR_EXPRESSION "${_sif_keep_pattern_fortran}"
    FAIL_REGULAR_EXPRESSION "${_sif_excluded_pattern_fortran}"
  )

  # Routine-level INCLUDE_LIST (whitelist): only keep_routine should be
  # instrumented; excluded_routine, omitted from the include list, should not.
  # NOTE: this whitelist semantics only applies to Fortran. The C/C++ frontend
  # consults includelist exclusively to opt inline functions in to
  # instrumentation, not as a global routine whitelist.
  add_sif_test(inclusion_fortran
    fortran tests/fortran/sif_excl.f90 tests/sif/include_routine_f90.tau)
  set_tests_properties(check_sif_inclusion_fortran
    PROPERTIES
    PASS_REGULAR_EXPRESSION "${_sif_keep_pattern_fortran}"
    FAIL_REGULAR_EXPRESSION "${_sif_excluded_pattern_fortran}"
  )

  add_sif_test(file_exclude_fortran
    fortran tests/fortran/sif_excl.f90 tests/sif/file_exclude_f90.tau)
  set_tests_properties(check_sif_file_exclude_fortran
    PROPERTIES
    FAIL_REGULAR_EXPRESSION "TAU_PROFILE_TIMER"
  )

  add_sif_test(file_include_match_fortran
    fortran tests/fortran/sif_excl.f90 tests/sif/file_include_match_f90.tau)
  set_tests_properties(check_sif_file_include_match_fortran
    PROPERTIES
    PASS_REGULAR_EXPRESSION "${_sif_keep_pattern_fortran}"
  )
  add_sif_test(file_include_nomatch_fortran
    fortran tests/fortran/sif_excl.f90 tests/sif/file_include_nomatch_f90.tau)
  set_tests_properties(check_sif_file_include_nomatch_fortran
    PROPERTIES
    FAIL_REGULAR_EXPRESSION "TAU_PROFILE_TIMER"
  )

  # Regression guards for the Fortran matcher's full-string semantics.
  # The flang plugin previously used regex_search (substring match), which
  # caused bare entries like "routine" or "excl" to silently match more
  # subprograms or files than intended. See issue #47.

  # A SIF entry that is only a substring of subprogram names must NOT
  # exclude them. Both keep_routine and excluded_routine should remain
  # instrumented after applying the SIF.
  add_sif_test(routine_substring_nomatch_fortran
    fortran tests/fortran/sif_excl.f90 tests/sif/exclude_substring_routine_f90.tau)
  set_tests_properties(check_sif_routine_substring_nomatch_fortran
    PROPERTIES
    PASS_REGULAR_EXPRESSION "${_sif_excluded_pattern_fortran}"
  )

  # Mirror of the EXCLUDE substring guard for the INCLUDE side: a bare
  # entry "routine" must NOT whitelist keep_routine or excluded_routine
  # via substring matching. With full-string semantics neither subprogram
  # is on the whitelist, so neither timer should appear.
  add_sif_test(routine_substring_include_nomatch_fortran
    fortran tests/fortran/sif_excl.f90 tests/sif/include_substring_routine_f90.tau)
  set_tests_properties(check_sif_routine_substring_include_nomatch_fortran
    PROPERTIES
    FAIL_REGULAR_EXPRESSION "${_sif_keep_pattern_fortran};${_sif_excluded_pattern_fortran}"
  )

  # A FILE_EXCLUDE entry that is only a substring of the source basename
  # must NOT exclude the file. Subprogram timers must still be emitted.
  add_sif_test(file_exclude_substring_nomatch_fortran
    fortran tests/fortran/sif_excl.f90 tests/sif/file_exclude_substring_f90.tau)
  set_tests_properties(check_sif_file_exclude_substring_nomatch_fortran
    PROPERTIES
    PASS_REGULAR_EXPRESSION "${_sif_keep_pattern_fortran}"
  )

  # A FILE_INCLUDE entry that is only a substring of the source basename
  # must NOT whitelist the file, so no TAU timers should appear.
  add_sif_test(file_include_substring_nomatch_fortran
    fortran tests/fortran/sif_excl.f90 tests/sif/file_include_substring_f90.tau)
  set_tests_properties(check_sif_file_include_substring_nomatch_fortran
    PROPERTIES
    FAIL_REGULAR_EXPRESSION "TAU_PROFILE_TIMER"
  )

  # FILE_INCLUDE_LIST with an actual `*` glob. The basename "sif_excl.f90"
  # matches "sif_*.f90", so the file is whitelisted and keep_routine is
  # instrumented. Exercises the glob-to-regex expansion path on file
  # matching, separate from the literal-basename file_include_match test.
  add_sif_test(file_include_glob_fortran
    fortran tests/fortran/sif_excl.f90 tests/sif/file_include_glob_f90.tau)
  set_tests_properties(check_sif_file_include_glob_fortran
    PROPERTIES
    PASS_REGULAR_EXPRESSION "${_sif_keep_pattern_fortran}"
  )

  # FILE_EXCLUDE_LIST with an actual `*` glob. The basename "sif_excl.f90"
  # matches "sif_*.f90", so the file is excluded and no TAU timers are
  # emitted. Mirror of file_include_glob_fortran for the exclude path.
  add_sif_test(file_exclude_glob_fortran
    fortran tests/fortran/sif_excl.f90 tests/sif/file_exclude_glob_f90.tau)
  set_tests_properties(check_sif_file_exclude_glob_fortran
    PROPERTIES
    FAIL_REGULAR_EXPRESSION "TAU_PROFILE_TIMER"
  )

  # Wildcard sanity check: a "#" kleene-star pattern must still match the
  # subprograms whose suffix it captures, so neither keep_routine nor
  # excluded_routine should appear with a TAU timer (only main remains).
  add_sif_test(routine_wildcard_excludes_both_fortran
    fortran tests/fortran/sif_excl.f90 tests/sif/exclude_wildcard_routine_f90.tau)
  set_tests_properties(check_sif_routine_wildcard_excludes_both_fortran
    PROPERTIES
    FAIL_REGULAR_EXPRESSION "${_sif_keep_pattern_fortran};${_sif_excluded_pattern_fortran}"
  )

  # Combined INCLUDE + EXCLUDE: per the TAU SIF spec the exclude list
  # operates on whatever survives the include filter. The SIF below
  # whitelists "#routine" (matches both subprograms) and then explicitly
  # excludes excluded_routine. Only keep_routine should retain its timer.
  add_sif_test(include_wildcard_exclude_specific_fortran
    fortran tests/fortran/sif_excl.f90 tests/sif/include_wildcard_exclude_specific_f90.tau)
  set_tests_properties(check_sif_include_wildcard_exclude_specific_fortran
    PROPERTIES
    PASS_REGULAR_EXPRESSION "${_sif_keep_pattern_fortran}"
    FAIL_REGULAR_EXPRESSION "${_sif_excluded_pattern_fortran}"
  )
endif()

set(SALT_COMPILERS_TO_TEST gcc clang)
# Per-toolchain build subdirs (GCC, LLVM) shared by C/C++ and Fortran tests.
foreach(tc_name IN ITEMS GCC LLVM)
  add_test(NAME setup_${tc_name}_dir
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/${tc_name})
  set_tests_properties(setup_${tc_name}_dir
    PROPERTIES
    FIXTURES_SETUP ${tc_name}_dir
    LABELS "toolchain:${tc_name};phase:setup"
  )
endforeach()

if(HAVE_TAU)
  # Resolve test source language to TAU wrapper + on-disk path. Sets
  # ${out_tauc_var} and ${out_path_var} in the caller's scope.
  function(_salt_tau_resolve_lang test_base_name test_lang
      out_tauc_var out_path_var)
    if(${test_lang} MATCHES "^[cC]$")
      set(tauc_local ${TAUCC})
      set(path_local ${CMAKE_SOURCE_DIR}/tests/${test_base_name}.${test_lang})
    elseif(${test_lang} MATCHES "^(cpp|CPP)$")
      set(tauc_local ${TAUCXX})
      set(path_local ${CMAKE_SOURCE_DIR}/tests/${test_base_name}.${test_lang})
    elseif(${test_lang} MATCHES "^[fF](90)?$")
      set(tauc_local ${TAUF90})
      set(path_local
        ${CMAKE_SOURCE_DIR}/tests/fortran/${test_base_name}.${test_lang})
    else()
      message(FATAL_ERROR "Unknown test source file extension: ${test_lang}")
    endif()
    set(${out_tauc_var} ${tauc_local} PARENT_SCOPE)
    set(${out_path_var} ${path_local} PARENT_SCOPE)
  endfunction()

  # Emit profile-dir cleanup + creation fixture pair. `comp_id`/`test_id` go
  # into test names; `test_base` names the .d profile dir; `dir` is the
  # toolchain build subdir (GCC, LLVM); `fixture_name` is the FIXTURES_SETUP
  # value; `parent_dep` is the test the rm step DEPENDS on.
  function(_salt_tau_emit_profile_dir_fixture
      comp_id test_id test_base dir
      lang_label tc_label fixture_name parent_dep)
    add_test(NAME rm_old_${comp_id}_${test_id}_profiles
      COMMAND ${CMAKE_COMMAND} -E rm -rf ${test_base}.d
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${dir}
    )
    set_tests_properties(rm_old_${comp_id}_${test_id}_profiles
      PROPERTIES
      FIXTURES_SETUP ${fixture_name}
      DEPENDS ${parent_dep}
      LABELS "${lang_label};${tc_label};phase:setup"
    )
    add_test(NAME mkdir_${test_base}.${comp_id}.d
      COMMAND ${CMAKE_COMMAND} -E make_directory ${test_base}.d
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${dir}
    )
    set_tests_properties(mkdir_${test_base}.${comp_id}.d
      PROPERTIES
      FIXTURES_SETUP ${fixture_name}
      DEPENDS rm_old_${comp_id}_${test_id}_profiles
      LABELS "${lang_label};${tc_label};phase:setup"
    )
  endfunction()

  # Emit run + check_*_profile pair. Keyword args:
  #   COMP_ID/TEST_ID  — go into emitted test names.
  #   TEST_BASE        — names the .d profile dir.
  #   DIR              — toolchain build subdir (GCC, LLVM).
  #   BINARY           — executable basename in ${CMAKE_BINARY_DIR}/${DIR}.
  #   LANG_LABEL/TC_LABEL — ctest LABELS.
  #   TAU_MAKEFILE     — resolved TAU Makefile path.
  #   COMPILE_DEP      — DEPENDS for run_*.
  #   PROFILE_FIX      — FIXTURES_REQUIRED set name (profile-dir fixture).
  function(_salt_tau_emit_run_check)
    set(kw COMP_ID TEST_ID TEST_BASE DIR BINARY LANG_LABEL TC_LABEL
        TAU_MAKEFILE COMPILE_DEP PROFILE_FIX)
    cmake_parse_arguments(ARG "" "${kw}" "" ${ARGN})
    _salt_tau_exec_tags(_run_tau_tags "${ARG_TAU_MAKEFILE}")
    add_test(NAME run_${ARG_COMP_ID}_${ARG_TEST_ID}
      COMMAND ${TAU_EXEC} -T ${_run_tau_tags} ./${ARG_BINARY}
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${ARG_DIR}
    )
    set_tests_properties(run_${ARG_COMP_ID}_${ARG_TEST_ID}
      PROPERTIES
      ENVIRONMENT
        "TAU_MAKEFILE=${ARG_TAU_MAKEFILE};PROFILEDIR=${ARG_TEST_BASE}.d"
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${TAU_BIN_DIR}"
      DEPENDS ${ARG_COMPILE_DEP}
      FIXTURES_REQUIRED ${ARG_PROFILE_FIX}
      LABELS "${ARG_LANG_LABEL};${ARG_TC_LABEL};phase:run"
      FAIL_REGULAR_EXPRESSION
        "[Cc]ommand not found;[Ss]egmentation;[Ff]ault;[Ee]rror"
    )
    add_test(NAME check_${ARG_COMP_ID}_${ARG_TEST_ID}_profile
      COMMAND ${CMAKE_COMMAND} -E cat ./${ARG_TEST_BASE}.d/profile.0.0.0
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${ARG_DIR}
    )
    set_tests_properties(check_${ARG_COMP_ID}_${ARG_TEST_ID}_profile
      PROPERTIES
      PASS_REGULAR_EXPRESSION "GROUP=\"TAU_DEFAULT\""
      FAIL_REGULAR_EXPRESSION "addr=\<0x"
      DEPENDS run_${ARG_COMP_ID}_${ARG_TEST_ID}
      LABELS "${ARG_LANG_LABEL};${ARG_TC_LABEL};phase:check"
    )
  endfunction()

  # Emit per-compiler TAU build/run/profile tests for one C/C++ test source.
  # Reads tauc/tauc_opts/compile_opts/compile_fail_patterns/_lang_label/
  # depends_on/test_name/test_base_name/test_lang/test_path/
  # _${test_base_name}_extra_sources from compile_instrumented's scope.
  function(_salt_tau_emit_compiler_tests comp)
    set(lower_comp ${comp})
    _salt_compiler_to_toolchain(${comp} tc_label)
    _salt_compiler_to_dir(${comp} dir)
    set(obj_fix clean_${lower_comp}_${test_name}_objects)
    set(prof_fix clean_${lower_comp}_${test_name}_profiles)
    add_test(NAME rm_${lower_comp}_${test_name}_objects
      COMMAND ${CMAKE_COMMAND} -E rm -rf
        ${test_base_name}.o
        ${test_base_name}.inst.o
        ${test_base_name}.inst.${test_lang}
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${dir}
    )
    set_tests_properties(rm_${lower_comp}_${test_name}_objects
      PROPERTIES
      FIXTURES_SETUP ${obj_fix}
      DEPENDS setup_${dir}_dir
      LABELS "${_lang_label};${tc_label};phase:setup"
    )
    # Per-test extra sources can be supplied via an
    # `_${test_base_name}_extra_sources` variable in the caller's scope
    # (e.g. forward_decl ships a stub TU that defines a symbol the fixture
    # deliberately leaves bodyless to exercise #53 regression coverage).
    add_test(NAME compile_${lower_comp}_${test_name}
      COMMAND ${tauc} ${tauc_opts} ${compile_opts}
        -o ${test_base_name}.${lower_comp}
        ${test_path} ${_${test_base_name}_extra_sources}
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${dir}
    )
    set_tests_properties(compile_${lower_comp}_${test_name}
      PROPERTIES
      ENVIRONMENT TAU_MAKEFILE=${TAU_${dir}_MAKEFILE}
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:${TAU_BIN_DIR}"
      FIXTURES_REQUIRED ${obj_fix}
      DEPENDS ${depends_on}
      LABELS "${_lang_label};${tc_label};phase:compile"
      FAIL_REGULAR_EXPRESSION "${compile_fail_patterns}"
    )
    _salt_tau_emit_profile_dir_fixture(
      ${lower_comp} ${test_name} ${test_base_name} ${dir}
      "${_lang_label}" "${tc_label}" ${prof_fix} setup_${dir}_dir)
    _salt_tau_emit_run_check(
      COMP_ID ${lower_comp}
      TEST_ID ${test_name}
      TEST_BASE ${test_base_name}
      DIR ${dir}
      BINARY ${test_base_name}.${lower_comp}
      LANG_LABEL "${_lang_label}"
      TC_LABEL "${tc_label}"
      TAU_MAKEFILE "${TAU_${dir}_MAKEFILE}"
      COMPILE_DEP compile_${lower_comp}_${test_name}
      PROFILE_FIX ${prof_fix})
  endfunction()

  # Add a TAU build/run/profile pipeline for a single test source. Optional
  # second argument overrides the test name (must match the prior
  # add_instrumentor_test invocation). Using the optional argument here and
  # in add_instrumentor_test is untested and may cause issues; a more robust
  # design might be to incorporate both functions into a single function.
  function(compile_instrumented test_src)
    get_filename_component(test_base_name ${test_src} NAME_WE)
    set(test_name "${test_base_name}")
    # Depends_on prevents concurrent runs from clobbering instrumented source.
    set(depends_on instrument_${test_base_name})
    get_filename_component(test_lang ${test_src} LAST_EXT)
    string(REPLACE "." "" test_lang ${test_lang})
    set(extra_args ${ARGN})
    list(LENGTH extra_args n_extra_args)
    if(n_extra_args GREATER 1)
      message(AUTHOR_WARNING
        "Incorrect number of arguments (1 + ${n_extra_args}) "
        "passed to add_instrumentor_test!")
      return()
    endif()
    if(n_extra_args EQUAL 1)
      set(test_name ${ARGV1})
      set(depends_on instrument_${ARGV1})
    endif()
    _salt_tau_resolve_lang(
      ${test_base_name} ${test_lang} tauc test_path)
    set(tauc_opts
      -optVerbose -optNoRevert -optSaltInst
      -optSaltParser=${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/saltfm)
    if(APPLE)
      list(APPEND tauc_opts -optShared)
    endif()
    set(compile_opts ${TAU_COMPILE_OPTIONS})
    # Each entry is one regex; CMake joins with `;` to form the
    # FAIL_REGULAR_EXPRESSION list (any matching pattern fails the test).
    set(compile_fail_patterns
      "[Dd]isabling instrumentation of source code"
      "[Ss]witching to compiler-based instrumentation"
      "[Cc]ompiling with [Nn]on-[Ii]nstrumented [Rr]egular [Cc]ode"
      "[Ee]rror:")
    _salt_ext_to_label(${test_src} _lang_label)
    foreach(comp IN LISTS SALT_COMPILERS_TO_TEST)
      _salt_tau_emit_compiler_tests(${comp})
    endforeach()
  endfunction()

  # This works, but a more thorough comparison of the fortran test setup
  # with the C/C++ setup is needed and the directory naming is awkward when
  # using Fortran sources in the above function.
  # list(APPEND TESTS_LIST
  #   funcsub.f90
  #   myhi.f
  # )

  # Pair forward_decl.c with a stub TU that defines the symbol the
  # fixture deliberately leaves bodyless.  cparse-llvm still only
  # parses forward_decl.c (so #53's bodyless-FunctionDecl path is
  # exercised); the stub satisfies the link so we can also assert the
  # instrumented binary runs and produces a TAU profile.
  set(_forward_decl_extra_sources ${CMAKE_SOURCE_DIR}/tests/forward_decl_stub.c)

  foreach(test_source IN LISTS TESTS_LIST)
    compile_instrumented(${test_source})
  endforeach()
endif()

if(TEST_FORTRAN)
  # Add some Fortran tests for SALT-FM
  set(FORTRAN_TESTS_SOURCES_LIST
    myhi.f
    cubes.f
    emptyprog.f90
    funcsub.f90
    hello.f90
    loop_test.f90
    trivial.f90
    return-only.f90
    if-stmt.f90
    if-stmt-edge.f90
    if-stmt-continuation.f90
    if-stmt-labeled.f90
    implicit-main.f90
    alternate-return.f90
    label-do-at-end.f90
    internal-func.f90
    interface_test.f90
    submodule_test.f90
    separate-module-procedure.f90
    pure-and-elemental.f90
  )

  # Sources that use obsolescent (but still standard) features
  # require relaxed compile flags: -Wpedantic emits warnings the
  # other tests treat as errors via -Werror.  Drop both for these
  # entries so the pipeline can compile, run, and profile them.
  set(FORTRAN_TESTS_PERMISSIVE_SOURCES
    alternate-return.f90
    label-do-at-end.f90
    separate-module-procedure.f90
  )

  # Add a smoke test of the fparse-llvm script
  add_test(NAME fparse_llvm_smoke_test
    COMMAND ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/fparse-llvm --help)
  set_tests_properties(fparse_llvm_smoke_test
    PROPERTIES
    LABELS "lang:Fortran;phase:smoke"
    PASS_REGULAR_EXPRESSION "USAGE"
  )

  foreach(test_source IN LISTS FORTRAN_TESTS_SOURCES_LIST)
    add_test(NAME instrument_${test_source}
      COMMAND
        ./${CMAKE_INSTALL_BINDIR}/saltfm
        ${CMAKE_SOURCE_DIR}/tests/fortran/${test_source}
    )
    set(_required_files
      "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/fparse-llvm"
      "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/saltfm")
    set_tests_properties(instrument_${test_source}
      PROPERTIES
      REQUIRED_FILES "${_required_files}"
      ENVIRONMENT "SALT_FORTRAN_VERBOSE=1"
      LABELS "lang:Fortran;phase:instrument"
      PASS_REGULAR_EXPRESSION "SALT Instrumentor Plugin finished"
    )
  endforeach()

  add_test(NAME check-internal-func.f90
    COMMAND ${CMAKE_COMMAND} -E cat ${CMAKE_BINARY_DIR}/internal-func.inst.F90
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  )
  set_tests_properties(check-internal-func.f90
    PROPERTIES
    PASS_REGULAR_EXPRESSION "[\n\r]+[\t ]+&add "
    DEPENDS instrument_internal-func.f90
    LABELS "lang:Fortran;phase:check"
  )

  # Implicit main programs (no program-stmt) have no name or source
  # line to attach to the timer.  The plugin synthesizes a fallback
  # name combining the input file's basename with a fixed
  # `_implicit_main_` marker, and uses the first-executable-stmt line
  # for the timer-name source range.  Pin both: the timer name must
  # contain `implicit-main._implicit_main_`, and the source-range
  # field must not start at line 0.
  add_test(NAME check_implicit_main_timer_name
    COMMAND ${CMAKE_COMMAND} -E cat ${CMAKE_BINARY_DIR}/implicit-main.inst.F90
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  )
  set_tests_properties(check_implicit_main_timer_name
    PROPERTIES
    PASS_REGULAR_EXPRESSION "implicit-main\\._implicit_main_"
    FAIL_REGULAR_EXPRESSION "\\[\\{[^}]*\\} \\{0,"
    DEPENDS instrument_implicit-main.f90
    LABELS "lang:Fortran;phase:check"
  )

  # Companion tests: the alternate-return values 1, 2, 3 must each
  # survive instrumentation (preserved verbatim in the synthesized
  # if-then-endif blocks for the if-stmt forms, and untouched on the
  # bare-return line).  Each value pins its own ctest because CMake's
  # PASS_REGULAR_EXPRESSION uses any-of semantics on a `;`-separated
  # list, not all-of.
  foreach(alt_value 1 2 3)
    add_test(NAME check_alternate-return-value-${alt_value}
      COMMAND ${CMAKE_COMMAND} -E cat ${CMAKE_BINARY_DIR}/alternate-return.inst.F90
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    set_tests_properties(check_alternate-return-value-${alt_value}
      PROPERTIES
      PASS_REGULAR_EXPRESSION "return ${alt_value}"
      DEPENDS instrument_alternate-return.f90
      LABELS "lang:Fortran;phase:check"
    )
  endforeach()

  # Per-form skip checks: each minimal source contains exactly one
  # pure / elemental / impure-elemental / module-procedure body and
  # nothing else.  The companion check_no_tau_<src> test reads back
  # the instrumented output and fails if any TAU_PROFILE_* call
  # appears, which is the structural assertion that the procedure
  # was skipped.  Less brittle than pinning warning text.
  set(FORTRAN_TESTS_PURE_SKIP_SOURCES
    pure-fn-only.f90
    elemental-fn-only.f90
    impure-elemental-only.f90
    mp-pure-only.f90
    mp-elemental-only.f90
  )

  foreach(test_source IN LISTS FORTRAN_TESTS_PURE_SKIP_SOURCES)
    get_filename_component(test_base_name ${test_source} NAME_WLE)
    get_filename_component(test_lang ${test_source} LAST_EXT)
    set(_inst_output ${CMAKE_BINARY_DIR}/${test_base_name}.inst${test_lang})
    string(REPLACE "f90" "F90" _inst_output "${_inst_output}")

    add_test(NAME instrument_${test_source}
      COMMAND
        ./${CMAKE_INSTALL_BINDIR}/saltfm
        ${CMAKE_SOURCE_DIR}/tests/fortran/${test_source}
    )
    set(_required_files
      "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/fparse-llvm"
      "${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/saltfm")
    set_tests_properties(instrument_${test_source}
      PROPERTIES
      REQUIRED_FILES "${_required_files}"
      ENVIRONMENT "SALT_FORTRAN_VERBOSE=1"
      LABELS "lang:Fortran;phase:instrument"
      PASS_REGULAR_EXPRESSION "SALT Instrumentor Plugin finished"
    )

    add_test(NAME check_no_tau_${test_source}
      COMMAND ${CMAKE_COMMAND} -E cat ${_inst_output}
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
    set_tests_properties(check_no_tau_${test_source}
      PROPERTIES
      FAIL_REGULAR_EXPRESSION "tauProfileTimer;TAU_PROFILE"
      DEPENDS instrument_${test_source}
      LABELS "lang:Fortran;phase:check"
    )
  endforeach()

  set(SALT_FORTRAN_COMPILERS_TO_TEST gfortran flang-new)

  if(HAVE_TAU)
    set(TAU_F90_OPTS
      -optVerbose -optNoRevert -optSaltInst
      -optSaltParser=${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/saltfm)
    if(APPLE)
      list(APPEND TAU_F90_OPTS -optShared)
    endif()
    foreach(test_source IN LISTS FORTRAN_TESTS_SOURCES_LIST)
      # Get the name of the instrumented source file
      get_filename_component(test_base_name ${test_source} NAME_WLE)
      get_filename_component(test_lang ${test_source} LAST_EXT)
      # fparse-llvm is adding preprocessor directives and should emit
      # uppercase file extensions (e.g., .F90)
      set(TEST_INST_SOURCE
        ${CMAKE_SOURCE_DIR}/tests/fortran/${test_base_name}${test_lang})

      foreach(compiler IN LISTS SALT_FORTRAN_COMPILERS_TO_TEST)
        _salt_compiler_to_toolchain(${compiler} tc_label)
        _salt_compiler_to_dir(${compiler} dir)
        string(TOUPPER ${compiler} upper_comp)
        if(${compiler} STREQUAL "gfortran")
          set(EXTRA_FLAGS
            -cpp -Wpedantic -Wextra -Wno-unused-function
            -Wno-missing-include-dirs ${SALT_WERROR})
        elseif(${compiler} MATCHES "^flang(-new)?$")
          set(EXTRA_FLAGS ${SALT_WERROR})
        else()
          message(FATAL_ERROR "Unknown compiler: ${compiler}")
        endif()
        # Sources that use obsolescent (but standard) features need
        # -Wpedantic and -Werror dropped so the warning does not block
        # the build.
        if(test_source IN_LIST FORTRAN_TESTS_PERMISSIVE_SOURCES)
          list(REMOVE_ITEM EXTRA_FLAGS "-Wpedantic" ${SALT_WERROR})
        endif()
        set(PROF_FIX clean_${upper_comp}_${test_source}_profiles)
        add_test(NAME compile_${upper_comp}_${test_source}
          COMMAND ${TAUF90} ${TAU_F90_OPTS} -o ${test_base_name}
            -Wall ${EXTRA_FLAGS} ${TEST_INST_SOURCE}
          WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${dir}
        )
        set_tests_properties(compile_${upper_comp}_${test_source}
          PROPERTIES
          ENVIRONMENT "TAU_MAKEFILE=${TAU_${dir}_MAKEFILE}"
          ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:${TAU_BIN_DIR}"
          DEPENDS "instrument_${test_source}"
          FIXTURES_REQUIRED ${dir}_dir
          LABELS "lang:Fortran;${tc_label};phase:compile"
          FAIL_REGULAR_EXPRESSION "[^W][Ee]rror"
        )
        _salt_tau_emit_profile_dir_fixture(
          ${upper_comp} ${test_source} ${test_base_name} ${dir}
          "lang:Fortran" "${tc_label}" ${PROF_FIX} setup_${dir}_dir)
        _salt_tau_emit_run_check(
          COMP_ID ${upper_comp}
          TEST_ID ${test_source}
          TEST_BASE ${test_base_name}
          DIR ${dir}
          BINARY ${test_base_name}
          LANG_LABEL "lang:Fortran"
          TC_LABEL "${tc_label}"
          TAU_MAKEFILE "${TAU_${dir}_MAKEFILE}"
          COMPILE_DEP compile_${upper_comp}_${test_source}
          PROFILE_FIX ${PROF_FIX})
      endforeach()
    endforeach()
  endif()
endif()
