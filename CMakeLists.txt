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
# supported" warnings from <__configuration/availability.h>.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")

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
find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

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
message(STATUS "CLANG_HEADER_INCLUDES: ${CLANG_HEADER_INCLUDES}")
message(STATUS "NUM_CLANG_HEADER_INCLUDES: ${NUM_CLANG_HEADER_INCLUDES}")

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
  set(SALT_MOD_SUFFIX ".saltmod" CACHE STRING "TAU Root Directory")
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

# Check if TAU_ROOT is set as an environment variable and if not set it as a
# CMake cache variable to /usr/local; otherwise, use the value from the
# environment.
set(TAU_PATH_SUFFIXES x86_64 x86_64/bin craycnl craycnl/bin apple apple/bin)
if(NOT DEFINED ENV{TAU_ROOT})
  find_program(TAU_EXEC tau_exec
    PATH_SUFFIXES ${TAU_PATH_SUFFIXES}
  )
  if(NOT TAU_EXEC)
    message(STATUS "TAU not found. Please set TAU_ROOT to the TAU installation directory.")
  else()
    get_filename_component(TAU_ROOT ${TAU_EXEC} DIRECTORY) # This will be a bin directory
    get_filename_component(TAU_ROOT ${TAU_ROOT} DIRECTORY) # This might be an arch directory
    string(REGEX REPLACE "(/x86_64$)|(/apple$)|(/craycnl$)" "" TAU_ROOT ${TAU_ROOT})
  endif()
else()
  set(TAU_ROOT $ENV{TAU_ROOT} CACHE PATH "TAU Root Directory")
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

  find_file(TAU_CLANG_MAKEFILE
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
  foreach(tau_var TAU_CLANG_MAKEFILE TAU_GCC_MAKEFILE TAUCC TAUCXX TAUF90)
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
)

include(CTest)

# Smoke test to check instrumentor compiled and will run
add_test(NAME salt_parser_smoke_test
  COMMAND $<TARGET_FILE:cparse-llvm> --help)
set_tests_properties(salt_parser_smoke_test
  PROPERTIES
  LABELS smoke
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
  add_test(NAME ${test_name}
    COMMAND
      ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/saltfm
      ${CMAKE_SOURCE_DIR}/tests/${test_src})
  set_tests_properties(${test_name}
    PROPERTIES
    REQUIRED_FILES "${CMAKE_SOURCE_DIR}/tests/${test_src}"
    PASS_REGULAR_EXPRESSION "[Ii]nstrumentation:"
  )
  add_test(NAME ${test_name}_exists
    COMMAND ${CMAKE_COMMAND} -E cat ./${test_base_name}.inst${test_lang}
  )
  set_tests_properties(${test_name}_exists
    PROPERTIES
    DEPENDS ${test_name}
    PASS_REGULAR_EXPRESSION "TAU_PROFILE_SET_NODE"
  )
endfunction()

foreach(test_source IN LISTS TESTS_LIST)
  add_instrumentor_test(${test_source})
endforeach()

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
    PASS_REGULAR_EXPRESSION "${instrument_pass_pattern}"
  )
  add_test(NAME check_${name}
    COMMAND ${CMAKE_COMMAND} -E cat ./${out_file}
  )
  set_tests_properties(check_${name}
    PROPERTIES
    DEPENDS instrument_${name}
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
  add_test(NAME instrument_${name}
    COMMAND ${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/saltfm
            --tau_select_file=${CMAKE_SOURCE_DIR}/${sif}
            --tau_output=${out_file}
            ${CMAKE_SOURCE_DIR}/${src}
  )
  set_tests_properties(instrument_${name}
    PROPERTIES
    REQUIRED_FILES "${CMAKE_SOURCE_DIR}/${src};${CMAKE_SOURCE_DIR}/${sif}"
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
foreach(comp IN LISTS SALT_COMPILERS_TO_TEST)
  set(SALT_LOWER_COMP ${comp})
  string(TOUPPER ${comp} comp)
  add_test(NAME setup_${comp}_dir
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/${comp})
endforeach()

if(HAVE_TAU)
  # Add a TAU build/run/profile pipeline for a single test source. Optional
  # second argument overrides the test name (must match the prior
  # add_instrumentor_test invocation). Using the optional argument here and
  # in add_instrumentor_test is untested and may cause issues; a more robust
  # design might be to incorporate both functions into a single function.
  function(compile_instrumented test_src)
    get_filename_component(test_base_name ${test_src} NAME_WE)
    set(test_name "${test_base_name}")
    # This next line depends on the previous function implementation; right
    # now this is just to prevent tests from running at the same time and
    # clobbering the instrumented source file.
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
      # This next line depends on the previous function implementation
      set(depends_on instrument_${ARGV1})
    endif()

    if(${test_lang} MATCHES "^[cC]$")
      set(tauc ${TAUCC})
      set(test_path ${CMAKE_SOURCE_DIR}/tests/${test_base_name}.${test_lang})
    elseif(${test_lang} MATCHES "^(cpp|CPP)$")
      set(tauc ${TAUCXX})
      set(test_path ${CMAKE_SOURCE_DIR}/tests/${test_base_name}.${test_lang})
    elseif(${test_lang} MATCHES "^[fF](90)?$")
      set(tauc ${TAUF90})
      set(test_path
        ${CMAKE_SOURCE_DIR}/tests/fortran/${test_base_name}.${test_lang})
    else()
      message(FATAL_ERROR "Unknown test source file extension: ${test_lang}")
    endif()

    set(tauc_opts
      -optVerbose -optNoRevert -optSaltInst
      -optSaltParser=${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/saltfm)
    set(compile_opts ${TAU_COMPILE_OPTIONS})
    # Each entry below is one regex; CMake joins them with `;` to form the
    # FAIL_REGULAR_EXPRESSION list (any matching pattern fails the test).
    set(compile_fail_patterns
      "[Dd]isabling instrumentation of source code"
      "[Ss]witching to compiler-based instrumentation"
      "[Cc]ompiling with [Nn]on-[Ii]nstrumented [Rr]egular [Cc]ode"
      "[Ee]rror:")
    foreach(comp IN LISTS SALT_COMPILERS_TO_TEST)
      set(lower_comp ${comp})
      string(TOUPPER ${comp} comp)
      # Fixture to cleanup old instrumented source, object files, and executables
      add_test(NAME rm_${lower_comp}_${test_name}_objects
        COMMAND ${CMAKE_COMMAND} -E rm -rf
          ${test_base_name}.o
          ${test_base_name}.inst.o
          ${test_base_name}.inst.${test_lang}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${comp}
      )
      set_tests_properties(rm_${lower_comp}_${test_name}_objects
        PROPERTIES
        FIXTURES_SETUP clean_${lower_comp}_${test_name}_objects
        DEPENDS setup_${comp}_dir
      )
      # Test to actually instrument and build the test source using TAU
      # compiler wrappers & salt parser.
      add_test(NAME compile_${lower_comp}_${test_name}
        COMMAND ${tauc} ${tauc_opts} ${compile_opts}
          -o ${test_base_name}.${lower_comp} ${test_path}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${comp}
      )
      set_tests_properties(compile_${lower_comp}_${test_name}
        PROPERTIES
        ENVIRONMENT TAU_MAKEFILE=${TAU_${comp}_MAKEFILE}
        FIXTURES_REQUIRED clean_${lower_comp}_${test_name}_objects
        DEPENDS ${depends_on}
        FAIL_REGULAR_EXPRESSION "${compile_fail_patterns}"
      )
      # Fixture to cleanup old profile directories
      add_test(NAME rm_old_${lower_comp}_${test_name}_profiles
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${test_base_name}.d
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${comp}
      )
      set_tests_properties(rm_old_${lower_comp}_${test_name}_profiles
        PROPERTIES
        FIXTURES_SETUP clean_${lower_comp}_${test_name}_profiles
        DEPENDS setup_${comp}_dir
      )
      add_test(NAME mkdir_${test_base_name}.${lower_comp}.d
        COMMAND ${CMAKE_COMMAND} -E make_directory ${test_base_name}.d
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${comp}
      )
      set_tests_properties(mkdir_${test_base_name}.${lower_comp}.d
        PROPERTIES
        FIXTURES_SETUP clean_${lower_comp}_${test_name}_profiles
        DEPENDS rm_old_${lower_comp}_${test_name}_profiles
      )
      # Profile w/ TAU and Verify profiles are created
      add_test(NAME run_${lower_comp}_${test_name}
        COMMAND ${TAU_EXEC} -T serial,pthread ./${test_base_name}.${lower_comp}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${comp}
      )
      set_tests_properties(run_${lower_comp}_${test_name}
        PROPERTIES
        ENVIRONMENT
          "TAU_MAKEFILE=${TAU_${comp}_MAKEFILE};PROFILEDIR=${test_base_name}.d"
        DEPENDS compile_${lower_comp}_${test_name}
        FIXTURES_REQUIRED clean_${lower_comp}_${test_name}_profiles
        FAIL_REGULAR_EXPRESSION
          "[Cc]ommand not found;[Ss]egmentation;[Ff]ault;[Ee]rror"
      )
      add_test(NAME check_${lower_comp}_${test_name}_profile
        COMMAND ${CMAKE_COMMAND} -E cat ./${test_base_name}.d/profile.0.0.0
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${comp}
      )
      set_tests_properties(check_${lower_comp}_${test_name}_profile
        PROPERTIES
        PASS_REGULAR_EXPRESSION "GROUP=\"TAU_DEFAULT\""
        FAIL_REGULAR_EXPRESSION "addr=\<0x"
        DEPENDS run_${lower_comp}_${test_name}
      )
    endforeach()
  endfunction()

  # This works, but a more thorough comparison of the fortran test setup
  # with the C/C++ setup is needed and the directory naming is awkward when
  # using Fortran sources in the above function.
  # list(APPEND TESTS_LIST
  #   funcsub.f90
  #   myhi.f
  # )

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
    alternate-return.f90
    label-do-at-end.f90
    internal-func.f90
    interface_test.f90
    submodule_test.f90
    separate-module-procedure.f90
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
    LABELS smoke
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
  )

  # Companion test: when running the alternate-return source through
  # SALT, the plugin must emit the issue-#32 informational note to
  # stderr for both flavors -- bare `return <expr>` and
  # `if (cond) return <expr>`.  CMake matches all listed
  # PASS_REGULAR_EXPRESSION patterns, so both message variants must
  # appear in the output.
  add_test(NAME warn_alternate-return.f90
    COMMAND
      ./${CMAKE_INSTALL_BINDIR}/saltfm
      ${CMAKE_SOURCE_DIR}/tests/fortran/alternate-return.f90
  )
  set_tests_properties(warn_alternate-return.f90
    PROPERTIES
    PASS_REGULAR_EXPRESSION
      "instrumenting .if .<cond>. return <expr>.;instrumenting .return <expr>."
    DEPENDS instrument_alternate-return.f90
  )

  set(SALT_FORTRAN_COMPILERS_TO_TEST gfortran flang-new)
  foreach(compiler IN LISTS SALT_FORTRAN_COMPILERS_TO_TEST)
    string(TOUPPER ${compiler} upper_comp)
    add_test(NAME setup_${compiler}_dir
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/${upper_comp})
    set_tests_properties(setup_${compiler}_dir
      PROPERTIES
      FIXTURES_SETUP ${upper_comp}_dir
    )
  endforeach()

  # TODO use the generic function above to add and process the Fortran tests.
  if(HAVE_TAU)
    set(TAU_F90_OPTS
      -optVerbose -optNoRevert -optSaltInst
      -optSaltParser=${CMAKE_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}/saltfm)
    foreach(test_source IN LISTS FORTRAN_TESTS_SOURCES_LIST)
      # Get the name of the instrumented source file
      get_filename_component(test_base_name ${test_source} NAME_WLE)
      get_filename_component(test_lang ${test_source} LAST_EXT)
      # fparse-llvm is adding preprocessor directives and should emit
      # uppercase file extensions (e.g., .F90)
      set(TEST_INST_SOURCE
        ${CMAKE_SOURCE_DIR}/tests/fortran/${test_base_name}${test_lang})

      foreach(compiler IN LISTS SALT_FORTRAN_COMPILERS_TO_TEST)
        string(TOUPPER ${compiler} upper_comp)
        if(${compiler} STREQUAL "gfortran")
          set(MAPPED_COMP GCC)
          set(EXTRA_FLAGS
            -cpp -Wpedantic -Wextra -Wno-unused-function
            -Wno-missing-include-dirs ${SALT_WERROR})
        elseif(${compiler} STREQUAL "flang-new")
          set(MAPPED_COMP CLANG)
          set(EXTRA_FLAGS ${SALT_WERROR})
        elseif(${compiler} STREQUAL "flang")
          set(MAPPED_COMP CLANG)
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
        add_test(NAME compile_${upper_comp}_${test_source}
          COMMAND ${TAUF90} ${TAU_F90_OPTS} -o ${test_base_name}
            -Wall ${EXTRA_FLAGS} ${TEST_INST_SOURCE}
          WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${upper_comp}
        )
        set_tests_properties(compile_${upper_comp}_${test_source}
          PROPERTIES
          ENVIRONMENT "TAU_MAKEFILE=${TAU_${MAPPED_COMP}_MAKEFILE}"
          DEPENDS "instrument_${test_source}"
          FIXTURES_REQUIRED ${upper_comp}_dir
          FAIL_REGULAR_EXPRESSION "[^W][Ee]rror"
        )
        # Profile with TAU and Verify profiles are created
        add_test(NAME run_${upper_comp}_${test_source}
          COMMAND ${TAU_EXEC} -T serial,pthread ./${test_base_name}
          WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${upper_comp}
        )
        set_tests_properties(run_${upper_comp}_${test_source}
          PROPERTIES
          ENVIRONMENT
            "TAU_MAKEFILE=${TAU_${MAPPED_COMP}_MAKEFILE};PROFILEDIR=${test_base_name}.d"
          DEPENDS compile_${upper_comp}_${test_source}
          FAIL_REGULAR_EXPRESSION
            "[Cc]ommand not found;[Ss]egmentation;[Ff]ault;[Ee]rror"
        )
        add_test(NAME check_${upper_comp}_${test_source}_profile
          COMMAND ${CMAKE_COMMAND} -E cat ./${test_base_name}.d/profile.0.0.0
          WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${upper_comp}
        )
        set_tests_properties(check_${upper_comp}_${test_source}_profile
          PROPERTIES
          PASS_REGULAR_EXPRESSION "GROUP=\"TAU_DEFAULT\""
          FAIL_REGULAR_EXPRESSION "addr=\<0x"
          DEPENDS run_${upper_comp}_${test_source}
        )
      endforeach()
    endforeach()
  endif()
endif()
