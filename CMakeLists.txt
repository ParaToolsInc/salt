#--------------------------------
# Generic CMake settings for SALT
#--------------------------------

cmake_minimum_required(VERSION 3.13.1)
# Ensure policies are set as they have been tested
cmake_policy(VERSION 3.13.1...3.23.2)
if(POLICY CMP0074)
  cmake_policy(SET CMP0074 NEW)
endif()
if(POLICY CMP0042)
  # Use MACOSX_RPATH
  cmake_policy(SET CMP0042 NEW)
  set(CMAKE_MACOSX_RPATH ON)
endif()

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15") # Catalina is oldest supported macOS

# Test if being built under a multi-config generator, i.e. and IDE like MSVS
get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
# Organize MSVS projects with folders
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
# Look for our custom CMake Modules here: cmake/modules
list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_CURRENT_LIST_DIR}/cmake/modules")

# Specify _INIT variables to override platform/compiler defaults
# See https://cmake.org/cmake/help/v3.11/variable/CMAKE_USER_MAKE_RULES_OVERRIDE.html
# Platform/compiler/project specific flags are set in the USER_MAKE_RULES_OVERRIDE file
set ( SALT_ALLOWED_BUILD_TYPES "Debug" "Release" "MinSizeRel" "RelWithDebInfo" "RuntimeDebug" )

if(NOT isMultiConfig)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build configuration" FORCE)
  elseif(NOT CMAKE_BUILD_TYPE IN_LIST SALT_ALLOWED_BUILD_TYPES)
    message(FATAL_ERROR "Invalid build type: ${CMAKE_BUILD_TYPE}")
  endif()
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY
    STRINGS "${SALT_ALLOWED_BUILD_TYPES}")
endif()


#------------------------------------------------------------
# SALT project specific global options, version, and defaults
#------------------------------------------------------------

# Ensure we're not building in the source tree root
include(CheckOutOfSourceBuild)
check_out_of_source_build()

# SALT project version
# Later we can get this from `git describe`
set (SALT_VERSION_MAJOR 0)
set (SALT_VERSION_MINOR 1)

# Print project logo, version and tag line
file(READ ${CMAKE_CURRENT_LIST_DIR}/cmake/SALT-logo.txt SALT_LOGO)
string(JOIN "\n         " SALT_GREETING "${SALT_LOGO}"
  "SALT, v${SALT_VERSION_MAJOR}.${SALT_VERSION_MINOR}: An LLVM-based Source Analysis Toolkit for HPC")
if(NOT isMultiConfig)
  string(APPEND SALT_GREETING "\n         Build type: ${CMAKE_BUILD_TYPE}")
endif()
string(APPEND SALT_GREETING "\n")
string(PREPEND SALT_GREETING "\n")
message(STATUS "${SALT_GREETING}")


#---------------------------------------------------------------------------------
# Get & print diagnostics about flags passed to CMake and build/source directories
#---------------------------------------------------------------------------------
message( STATUS "CMake source directory: ${CMAKE_SOURCE_DIR}")
message( STATUS "CMake binary directory: ${CMAKE_BINARY_DIR}")
include(GetPassedCMakeArgs)
get_passed_CMake_args()
if(CMAKE_ARGS)
  message("Passed command line CMAKE_ARGS: ${CMAKE_ARGS}")
endif()

#-----------------------------
# Create the main SALT project
#-----------------------------
project(SALT
  VERSION "${SALT_VERSION_MAJOR}.${SALT_VERSION_MINOR}"
  DESCRIPTION "An LLVM-based Source Analysis Tookit for HPC"
  HOMEPAGE_URL "https://github.com/ParaToolsInc/salt"
  LANGUAGES CXX C)

#----------
# Find LLVM
#----------

# See https://llvm.org/docs/CMake.html#embedding-llvm-in-your-project for more information
find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

# get_cmake_property(_variableNames VARIABLES)
# list (SORT _variableNames)
# foreach (_variableName ${_variableNames})
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
  message(STATUS "LLVM built with ENABLE_RTTI=FALSE, -fno-rtti will be passed when building & linking")
endif()
message(STATUS "LLVM_TOOLS_BINARY_DIR:\n\t${LLVM_TOOLS_BINARY_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})


list(LENGTH CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES NUM_CLANG_HEADER_INCLUDES)
list(JOIN CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES "\", \"-I" CLANG_HEADER_INCLUDES)
set(CLANG_HEADER_INCLUDES "\"-I${CLANG_HEADER_INCLUDES}\"")
message(STATUS "CLANG_HEADER_INCLUDES: ${CLANG_HEADER_INCLUDES}")
message(STATUS "NUM_CLANG_HEADER_INCLUDES: ${NUM_CLANG_HEADER_INCLUDES}")
configure_file(
  "${CMAKE_SOURCE_DIR}/include/clang_header_includes.h.in" "${CMAKE_BINARY_DIR}/include/clang_header_includes.h"
  @ONLY)

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
target_compile_features(SALT_LLVM_TOOLING INTERFACE cxx_std_14)
target_include_directories(SALT_LLVM_TOOLING INTERFACE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(SALT_LLVM_TOOLING INTERFACE ${LLVM_DEFINITIONS_LIST})
target_compile_options(SALT_LLVM_TOOLING INTERFACE ${USE_RTTI} -Wall -Werror -Wpedantic)
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
  tau_datatypes.h
  tooling.hpp
)

list(TRANSFORM SALT_HEADER_FILES PREPEND "${CMAKE_SOURCE_DIR}/include/")
list(APPEND SALT_HEADER_FILES "${CMAKE_BINARY_DIR}/include/clang_header_includes.h")
set_source_files_properties(
  "${CMAKE_BINARY_DIR}/include/clang_header_includes.h"
  PROPERTIES
  GENERATED TRUE
)

set(CPARSE_LLVM_SRCS
  frontend.cpp
  instrumentor.cpp
  selectfile.cpp
)

list(TRANSFORM CPARSE_LLVM_SRCS PREPEND "${CMAKE_SOURCE_DIR}/src/")

#---------------------
# Add the main targets
#---------------------
# If we refactor into a library that executables link against it will simplify this and reduce repitition
add_executable(cparse-llvm)
target_sources(cparse-llvm PUBLIC ${CPARSE_LLVM_SRCS})
target_include_directories(cparse-llvm PUBLIC "${CMAKE_SOURCE_DIR}/include" "${CMAKE_BINARY_DIR}/include")
target_compile_features(cparse-llvm PUBLIC cxx_std_14)
target_link_libraries(cparse-llvm PUBLIC SALT_LLVM_TOOLING) # Inherit definitions, compile features, etc.
target_link_options(cparse-llvm PUBLIC -static -Wl,--as-needed -Wl,--no-allow-shlib-undefined -Wl,--no-undefined)


#---------------
# Tests
#---------------
include(CTest)

# Smoke test to check instrumentor compiled and will run
add_test(NAME smoke_test
  COMMAND $<TARGET_FILE:cparse-llvm> --help)
set_tests_properties(smoke_test
  PROPERTIES
  LABELS smoke
  PASS_REGULAR_EXPRESSION "OVERVIEW"
)

# Test fixtures to copy and cleanup config files
add_test(NAME setup_inputs
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/config_files ./config_files)
add_test(NAME cleanup_inputs
  COMMAND ${CMAKE_COMMAND} -E rm -rf ./config_files)
set_tests_properties(setup_inputs PROPERTIES FIXTURES_SETUP Configs)
set_tests_properties(cleanup_inputs PROPERTIES FIXTURES_CLEANUP Configs)

# List of tests to run from ${CMAKE_SOURCE_DIR}/tests/
set(TESTS_LIST
  hello.c
  1d.c
  c11test.cpp
)

# Function to map tests_list to source files and setup tests
function(add_instrumentor_test test_src)
  get_filename_component(TEST_BASE_NAME ${test_src} NAME_WE)
  set(TEST_NAME "instrument_${TEST_BASE_NAME}")
  get_filename_component(TEST_LANG ${test_src} LAST_EXT)
  set(extra_args ${ARGN})
  list(LENGTH extra_args n_extra_args)
  if(n_extra_args GREATER 1)
    message(AUTHOR_WARNING
      "Incorrect number of arguments (1 + ${n_extra_args}) passed to add_instrumentor_test!")
    return()
  endif()
  if(n_extra_args EQUAL 1)
    set(TEST_NAME ${ARGV1})
  endif()
  add_test(NAME ${TEST_NAME}
    COMMAND $<TARGET_FILE:cparse-llvm> ${CMAKE_SOURCE_DIR}/tests/${test_src})
  set_tests_properties(${TEST_NAME}
    PROPERTIES
    FIXTURES_REQUIRED Configs
    REQUIRED_FILES "${CMAKE_SOURCE_DIR}/tests/${test_src}"
    PASS_REGULAR_EXPRESSION "Instrumentation: "
  )
  add_test(NAME ${TEST_NAME}_exists
    COMMAND ${CMAKE_COMMAND} -E cat ./${TEST_BASE_NAME}.inst${TEST_LANG}
  )
  set_tests_properties(${TEST_NAME}_exists
    PROPERTIES
    DEPENDS ${TEST_NAME}
    PASS_REGULAR_EXPERSSION "TAU_"
  )
endfunction()

foreach(test_source IN LISTS TESTS_LIST)
  add_instrumentor_test(${test_source})
endforeach()


set(TAU_COMPILE_OPTIONS -fPIC)
set(TAU_C_DEFINITIONS
-DEBS_CLOCK_RES=1
-DHAVE_GNU_DEMANGLE
-DHAVE_TR1_HASH_MAP
-DPROFILING_ON
-DPTHREADS
-DTAU_BFD
-DTAU_DOT_H_LESS_HEADERS
-DTAU_DWARF
-DTAU_ELF_BFD
-DTAU_LARGEFILE
-DTAU_LINUX_TIMERS
-DTAU_NO_FORTRAN
-DTAU_PTHREAD_PRELOAD
-DTAU_SS_ALLOC_SUPPORT
-DTAU_STRSIGNAL_OK
-DTAU_TRACK_LD_LOADER
-DTAU_UNIFY
-DTAU_UNWIND
-DTAU_USE_LIBUNWIND
-DTAU_USE_TLS
-D_LARGEFILE64_SOURCE
)
set(TAU_C_CLANG_DEFINITIONS
-DTAU_CLANG
)
set(TAU_HEADER_LOCATIONS
-I/usr/local/include
)
set(TAU_CLANG_HEADER_LOCATIONS
-I/usr/local/x86_64/libdwarf-clang/include
-I/usr/local/x86_64/libunwind-1.3.1-clang/include
)

find_file(TAU_CLANG_MAKEFILE Makefile.tau-clang-pthread
  PATH_SUFFIXES x86_64 x86_64/lib
)
find_file(TAU_GCC_MAKEFILE Makefile.tau-pthread
  PATH_SUFFIXES x86_64 x86_64/lib
)
find_program(TAUCC tau_cc.sh
  PATH_SUFFIXES x86_64 x86_64/bin
)
find_program(TAUCXX tau_cxx.sh
  PATH_SUFFIXES x86_64 x86_64/bin
)
find_program(TAU_EXEC tau_exec
  PATHS /usr/local/x86_64/bin/
)

function(clang_compile_instrumented test_src)
  get_filename_component(TEST_BASE_NAME ${test_src} NAME_WE)
  set(TEST_NAME "${TEST_BASE_NAME}")
  set(depends_on ${TEST_BASE_NAME})
  get_filename_component(TEST_LANG ${test_src} LAST_EXT)
  set(extra_args ${ARGN})
  list(LENGTH extra_args n_extra_args)
  if(n_extra_args GREATER 1)
    message(AUTHOR_WARNING
      "Incorrect number of arguments (1 + ${n_extra_args}) passed to add_instrumentor_test!")
    return()
  endif()
  if(n_extra_args EQUAL 1)
    set(TEST_NAME ${ARGV1})
    set(depends_on ${ARGV1})
  endif()

  set(opts ${TAU_COMPILE_OPTIONS})
  set(includes ${TAU_CLANG_HEADER_LOCATIONS} ${TAU_HEADER_LOCATIONS})
  set(defs ${TAU_C_DEFINITIONS} ${TAU_C_CLANG_DEFINITIONS})
  add_test(NAME compile_${TEST_NAME}
    COMMAND
    ${TAUCC} -optLinkOnly ${defs} ${includes} ${opts} -o ${TEST_BASE_NAME} ${TEST_BASE_NAME}.inst${TEST_LANG}
  )
  set_tests_properties(compile_${TEST_NAME}
    PROPERTIES
    ENVIRONMENT TAU_MAKEFILE=${TAU_CLANG_MAKEFILE}
    DEPENDS ${depends_on}
  )
  add_test(NAME rm_old_${TEST_NAME}_profiles
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${TEST_BASE_NAME}.d
  )
  set_tests_properties(rm_old_${TEST_NAME}_profiles PROPERTIES FIXTURES_SETUP clean_${TEST_NAME})
  add_test(NAME run_${TEST_NAME}
    COMMAND
    ${TAU_EXEC} -T serial,pthread ./${TEST_BASE_NAME}
  )
  set_tests_properties(run_${TEST_NAME}
    PROPERTIES
    ENVIRONMENT "TAU_MAKEFILE=${TAU_CLANG_MAKEFILE};PROFILEDIR=${TEST_BASE_NAME}.d"
    DEPENDS compile_${TEST_NAME}
    FIXTURES_REQUIRED clean_${TEST_NAME}
  )
  add_test(NAME check_${TEST_NAME}_profile
    COMMAND ${CMAKE_COMMAND} -E cat ./${TEST_BASE_NAME}.d/profile.0.0.0
  )
  set_tests_properties(check_${TEST_NAME}_profile
    PROPERTIES PASS_REGULAR_EXPRESSION "TAU_USER"
  )
endfunction()

clang_compile_instrumented(hello.c)
clang_compile_instrumented(1d.c)
#clang_compile_instrumented(c11.c)
