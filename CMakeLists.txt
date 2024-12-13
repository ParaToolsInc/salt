#--------------------------------
# Generic CMake settings for SALT
#--------------------------------

cmake_minimum_required(VERSION 3.13.1)
# Ensure policies are set as they have been tested
cmake_policy(VERSION 3.13.1...3.31.2)
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
set (SALT_VERSION_MINOR 2)

# Print project logo, version and tag line
file(READ ${CMAKE_CURRENT_LIST_DIR}/cmake/SALT-logo.txt SALT_LOGO)
string(JOIN "\n         " SALT_GREETING "${SALT_LOGO}"
  "SALT-FM, v${SALT_VERSION_MAJOR}.${SALT_VERSION_MINOR}: An LLVM-based Source Analysis Toolkit for HPC")
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

set(CMAKE_CXX_STANDARD 17)

#-----------------------------
# Create the main SALT project
#-----------------------------
project(SALT-FM
  VERSION "${SALT_VERSION_MAJOR}.${SALT_VERSION_MINOR}"
  DESCRIPTION "An LLVM-based Source Analysis Tookit for HPC"
  HOMEPAGE_URL "https://github.com/ParaToolsInc/salt"
  LANGUAGES CXX C)

#----------
# Find LLVM
#----------

# See https://llvm.org/docs/CMake.html#embedding-llvm-in-your-project for more information
# If LLVM and/or Clang installations are partial,
# then their supplied CMake exports files need to be patched.
# This is already done in the salt-dev docker container.
# The patches are kept as a git submodule at ./cmake/llvm-patches
# and the upstream at https://github.com/ParaToolsInc/salt-llvm-patches
# If either of the steps below fails and you have write access to the
# LLCV/Clang installations, try patching them using the supplied patches
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
target_compile_features(SALT_LLVM_TOOLING INTERFACE cxx_std_17)
target_include_directories(SALT_LLVM_TOOLING INTERFACE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(SALT_LLVM_TOOLING INTERFACE ${LLVM_DEFINITIONS_LIST})
target_compile_options(SALT_LLVM_TOOLING INTERFACE ${USE_RTTI} -Wall -Wpedantic $<$<CONFIG:Debug>:-Wno-gnu-zero-variadic-macro-arguments>)
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
target_compile_features(cparse-llvm PUBLIC cxx_std_17)
target_link_libraries(cparse-llvm PUBLIC SALT_LLVM_TOOLING) # Inherit definitions, compile features, etc.
# You can try adding -static
target_link_options(cparse-llvm PUBLIC -Wl,--as-needed -Wl,--no-allow-shlib-undefined -Wl,--no-undefined)
# Turn on debug output if a debug build is being built
target_compile_definitions(cparse-llvm PUBLIC $<$<CONFIG:Debug>:DEBUG_NO_WAY>)
# Install the target
install(TARGETS cparse-llvm DESTINATION bin)


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
                
# Variables set in FlangConfig.cmake
    message(STATUS "FLANG_CMAKE_DIR: ${FLANG_CMAKE_DIR}")
    message(STATUS "FLANG_EXPORTED_TARGETS: ${FLANG_EXPORTED_TARGETS}")
    message(STATUS "FLANG_INCLUDE_DIRS: ${FLANG_INCLUDE_DIRS}")

# Interface for Flang frontend plugins
    add_library(SALT_FLANG_FRONTEND INTERFACE)
    target_compile_features(SALT_FLANG_FRONTEND INTERFACE cxx_std_17)
    target_include_directories(SALT_FLANG_FRONTEND INTERFACE ${LLVM_INCLUDE_DIRS} ${FLANG_INCLUDE_DIRS})
    target_compile_definitions(SALT_FLANG_FRONTEND INTERFACE ${LLVM_DEFINITIONS_LIST})
    target_compile_options(SALT_FLANG_FRONTEND INTERFACE ${USE_RTTI} -Wall -Werror -Wpedantic)

# Endianness definitions are required, and Flang does not export a definitions list
    include(TestBigEndian)
    test_big_endian(IS_BIGENDIAN)
    if (IS_BIGENDIAN)
        target_compile_definitions(SALT_FLANG_FRONTEND INTERFACE FLANG_BIG_ENDIAN=1)
    else ()
        target_compile_definitions(SALT_FLANG_FRONTEND INTERFACE FLANG_LITTLE_ENDIAN=1)
    endif ()

    set(SALT_FLANG_PLUGIN_HEADER_FILES
    selectfile.hpp
    tau_datatypes.h
    )
    list(TRANSFORM SALT_FLANG_PLUGIN_HEADER_FILES PREPEND "${CMAKE_SOURCE_DIR}/include/")

    set(SALT_FLANG_PLUGIN_SRCS
    salt_instrument_flang_plugin.cpp
    )
    list(TRANSFORM SALT_FLANG_PLUGIN_SRCS PREPEND "${CMAKE_SOURCE_DIR}/src/")

    add_library(salt-flang-plugin SHARED)
    target_sources(salt-flang-plugin PUBLIC ${SALT_FLANG_PLUGIN_SRCS})
    target_include_directories(salt-flang-plugin PUBLIC "${CMAKE_SOURCE_DIR}/include" "${CMAKE_BINARY_DIR}/include" )
    target_compile_features(salt-flang-plugin PUBLIC cxx_std_17)
    target_link_libraries(salt-flang-plugin PUBLIC SALT_FLANG_FRONTEND)
    target_link_options(salt-flang-plugin PUBLIC -Wl,--as-needed -Wl,-undefined -Wl,dynamic_lookup)

    install(TARGETS salt-flang-plugin DESTINATION lib)

    configure_file(${CMAKE_SOURCE_DIR}/src/fparse-llvm.in ${CMAKE_BINARY_DIR}/fparse-llvm @ONLY)
    install(PROGRAMS ${CMAKE_BINARY_DIR}/fparse-llvm
      TYPE BIN)

else()
    message(STATUS "Flang not found -- skipping Flang frontend plugin")
endif()

#---------------------
# Find TAU locations for testing
#---------------------

# Check if TAU_ROOT is set as an environment variable and if not set it as a CMake cache variable to /usr/local
# otherwise, use the value from the environment
set(TAU_PATH_SUFFIXES x86_64 x86_64/bin craycnl craycnl/bin apple apple/bin)
if(NOT DEFINED ENV{TAU_ROOT})
  find_program(TAU_EXEC tau_exec
    PATH_SUFFIXES ${TAU_PATH_SUFFIXES}
  )
  if(NOT TAU_EXEC)
    message(FATAL_ERROR "TAU not found. Please set TAU_ROOT to the TAU installation directory.")
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
  REQUIRED
)
get_filename_component(TAU_ARCH_DIR ${TAU_EXEC} DIRECTORY)
get_filename_component(TAU_ARCH_DIR ${TAU_ARCH_DIR} DIRECTORY)
message(STATUS "TAU_ARCH_DIR: ${TAU_ARCH_DIR}")

find_file(TAU_CLANG_MAKEFILE
  NAMES Makefile.tau-clang-pthread
  PATHS ${TAU_ARCH_DIR}  PATH_SUFFIXES lib
  REQUIRED
)
find_file(TAU_GCC_MAKEFILE
  NAMES Makefile.tau-pthread Makefile.tau-pthread-pdt
  PATHS ${TAU_ARCH_DIR}  PATH_SUFFIXES lib
  REQUIRED
)
find_program(TAUCC tau_cc.sh
  PATHS ${TAU_ARCH_DIR}  PATH_SUFFIXES bin
  REQUIRED
)
find_program(TAUCXX tau_cxx.sh
  PATHS ${TAU_ARCH_DIR}  PATH_SUFFIXES bin
  REQUIRED
)
find_program(TAUF90 tau_f90.sh
  PATHS ${TAU_ARCH_DIR}  PATH_SUFFIXES bin
  REQUIRED
)
file(GLOB TAU_GCC_LIBUNWIND_DIR ${TAU_ARCH_DIR}/libunwind-*-gcc)
if(NOT TAU_GCC_LIBUNWIND_DIR)
  message(FATAL_ERROR "libunwind not found for TAU's gcc build")
endif()
find_path(TAU_GCC_LIBUNWIND_INCLUDE_DIR
  NAMES include
  PATHS ${TAU_GCC_LIBUNWIND_DIR}
  REQUIRED
)
file(GLOB TAU_CLANG_LIBUNWIND_DIR ${TAU_ARCH_DIR}/libunwind-*-clang)
if(NOT TAU_CLANG_LIBUNWIND_DIR)
  message(FATAL_ERROR "libunwind not found for TAU's clang build")
endif()
find_path(TAU_CLANG_LIBUNWIND_INCLUDE_DIR
  NAMES include
  PATHS ${TAU_CLANG_LIBUNWIND_DIR}
  REQUIRED
)
find_path(TAU_GCC_LIBDWARF_INCLUDE_DIR
  NAMES include
  PATHS ${TAU_ARCH_DIR}/libdwarf-gcc
  REQUIRED
)
find_path(TAU_CLANG_LIBDWARF_INCLUDE_DIR
  NAMES include
  PATHS ${TAU_ARCH_DIR}/libdwarf-clang
  REQUIRED
)

#---------------
# Tests
#---------------
# There are some assumptions made in testing:
# 1. Test source files to be instrumented are located in ${CMAKE_SOURCE_DIR}/tests/
# 2. Tests are currently not using MPI
# 3. Only pthread threading is assumed
# 4. TAU is installed into the default location of /usr/local/x86_64
# 5. TAU is installed with the following configuratins:
#    - Makefile.tau-clang-pthread
#    - Makefile.tau-pthread
# 6. SALT parser configuration files are in ${CMAKE_SOURCE_DIR}/config_files

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

# Test fixtures to copy and cleanup config files
add_test(NAME setup_inputs
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/config_files ./config_files)
add_test(NAME cleanup_inputs
  COMMAND ${CMAKE_COMMAND} -E rm -rf ./config_files)
set_tests_properties(setup_inputs PROPERTIES FIXTURES_SETUP Configs)
set_tests_properties(cleanup_inputs PROPERTIES FIXTURES_CLEANUP Configs)

# Function to map tests_list to source files and setup parser tests
function(add_instrumentor_test test_src)
  # This is 1 of 2 functions for adding tests
  # It is somewhat fragile/opinionated and passing the optional argument
  # will likely lead to more trouble than it's worth.
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
    PASS_REGULAR_EXPRESSION "[Ii]nstrumentation:"
  )
  add_test(NAME ${TEST_NAME}_exists
    COMMAND ${CMAKE_COMMAND} -E cat ./${TEST_BASE_NAME}.inst${TEST_LANG}
  )
  set_tests_properties(${TEST_NAME}_exists
    PROPERTIES
    DEPENDS ${TEST_NAME}
    PASS_REGULAR_EXPRESSION "TAU_PROFILE_SET_NODE"
  )
endfunction()

foreach(test_source IN LISTS TESTS_LIST)
  add_instrumentor_test(${test_source})
endforeach()

set(compilers_to_test gcc clang)
foreach(comp IN LISTS compilers_to_test)
  set(lower_comp ${comp})
  string(TOUPPER ${comp} comp)
  add_test(NAME setup_${comp}_dir
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/${comp})
endforeach()

function(compile_instrumented test_src)
  # This is the 2nd of 2 functions for adding tests.
  # It is opinionated and perhaps somewhat fragile.
  # Using the optional argument in this function and the previous one
  # is untested and may cause issues.
  # A more robust design might be to incorporate both functions into a
  # single function.
  get_filename_component(TEST_BASE_NAME ${test_src} NAME_WE)
  set(TEST_NAME "${TEST_BASE_NAME}")
  # This next line depends on the previous function implementation
  set(depends_on instrument_${TEST_BASE_NAME}) # Right now this is just to prevent tests from running at the same time from clobbering the instrumented source file
  get_filename_component(TEST_LANG ${test_src} LAST_EXT)
  string(REPLACE "." "" TEST_LANG ${TEST_LANG})
  set(extra_args ${ARGN})
  list(LENGTH extra_args n_extra_args)
  if(n_extra_args GREATER 1)
    message(AUTHOR_WARNING
      "Incorrect number of arguments (1 + ${n_extra_args}) passed to add_instrumentor_test!")
    return()
  endif()
  if(n_extra_args EQUAL 1)
    set(TEST_NAME ${ARGV1})
    # This next line depends on the previous function implementation
    set(depends_on instrument_${ARGV1})
  endif()

  if(${TEST_LANG} STREQUAL "c")
    set(TAUC ${TAUCC})
  elseif(${TEST_LANG} STREQUAL "cpp")
    set(TAUC ${TAUCXX})
  else()
    message( FATAL_ERROR "Unknown test source file extension: ${TEST_LANG}")
  endif()

  set(test_path ${CMAKE_SOURCE_DIR}/tests/${TEST_BASE_NAME}.${TEST_LANG})
  set(TAUC_OPTS -optVerbose -optSaltInst -optSaltParser=$<TARGET_FILE:cparse-llvm> -optSaltConfigFile=${CMAKE_SOURCE_DIR}/config_files/tau_config.yaml)
  set(compile_opts ${TAU_COMPILE_OPTIONS})
  foreach(comp IN LISTS compilers_to_test)
    set(lower_comp ${comp})
    string(TOUPPER ${comp} comp)
    # Fixture to cleanup old instrumented source, object files, and executables
    add_test(NAME rm_${lower_comp}_${TEST_NAME}_objects
      COMMAND
      ${CMAKE_COMMAND} -E rm -rf ${TEST_BASE_NAME}.o ${TEST_BASE_NAME}.inst.o ${TEST_BASE_NAME}.inst.${TEST_LANG}
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${comp}
    )
    set_tests_properties(rm_${lower_comp}_${TEST_NAME}_objects
      PROPERTIES
      FIXTURES_SETUP clean_${lower_comp}_${TEST_NAME}_objects
      DEPENDS setup_${comp}_dir
    )
    # Test to actually instrument and build the test source using TAU compiler wrappers & slat parser
    add_test(NAME compile_${lower_comp}_${TEST_NAME}
      COMMAND
      ${TAUC} ${TAUC_OPTS} ${compiler_opts} -o ${TEST_BASE_NAME}.${lower_comp} ${test_path}
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${comp}
    )
    set_tests_properties(compile_${lower_comp}_${TEST_NAME}
      PROPERTIES
      ENVIRONMENT TAU_MAKEFILE=${TAU_${comp}_MAKEFILE}
      FIXTURES_REQUIRED clean_${lower_comp}_${TEST_NAME}_objects
      DEPENDS ${depends_on}
      FAIL_REGULAR_EXPRESSION "[Dd]isabling instrumentation of source code;[Ss]witching to compiler-based instrumentation;[Cc]ompiling with [Nn]on-[Ii]nstrumented [Rr]egular [Cc]ode;[Ee]rror:"
    )
    # Fixture to cleanup old profile directories
    add_test(NAME rm_old_${lower_comp}_${TEST_NAME}_profiles
      COMMAND
      ${CMAKE_COMMAND} -E rm -rf ${TEST_BASE_NAME}.d
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${comp}
    )
    set_tests_properties(rm_old_${lower_comp}_${TEST_NAME}_profiles
      PROPERTIES
      FIXTURES_SETUP clean_${lower_comp}_${TEST_NAME}_profiles
      DEPENDS setup_${comp}_dir
    )
    add_test(NAME mkdir_${TEST_BASE_NAME}.${lower_comp}.d
      COMMAND
      ${CMAKE_COMMAND} -E make_directory ${TEST_BASE_NAME}.d
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${comp}
    )
    set_tests_properties(mkdir_${TEST_BASE_NAME}.${lower_comp}.d
      PROPERTIES
      FIXTURES_SETUP clean_${lower_comp}_${TEST_NAME}_profiles
      DEPENDS rm_old_${lower_comp}_${TEST_NAME}_profiles
    )
    # Profile w/ TAU and Verify profiles are created
    add_test(NAME run_${lower_comp}_${TEST_NAME}
      COMMAND
      ${TAU_EXEC} -T serial,pthread ./${TEST_BASE_NAME}.${lower_comp}
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${comp}
    )
    set_tests_properties(run_${lower_comp}_${TEST_NAME}
      PROPERTIES
      ENVIRONMENT "TAU_MAKEFILE=${TAU_${comp}_MAKEFILE};PROFILEDIR=${TEST_BASE_NAME}.d"
      DEPENDS compile_${lower_comp}_${TEST_NAME}
      FIXTURES_REQUIRED clean_${lower_comp}_${TEST_NAME}_profiles
      FAIL_REGULAR_EXPRESSION "[Cc]ommand not found;[Ss]egmentation;[Ff]ault;[Ee]rror"
    )
    add_test(NAME check_${lower_comp}_${TEST_NAME}_profile
      COMMAND ${CMAKE_COMMAND} -E cat ./${TEST_BASE_NAME}.d/profile.0.0.0
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${comp}
    )
    set_tests_properties(check_${lower_comp}_${TEST_NAME}_profile
      PROPERTIES
      PASS_REGULAR_EXPRESSION "GROUP=\"TAU_DEFAULT\""
      FAIL_REGULAR_EXPRESSION "addr=\<0x"
      DEPENDS run_${lower_comp}_${TEST_NAME}
    )
  endforeach()
endfunction()

foreach(test_source IN LISTS TESTS_LIST)
  compile_instrumented(${test_source})
endforeach()

# Add some Fortran tests for SALT-FM
set(FORTRAN_TESTS_SOURCES_LIST
  myhi.f
  cubes.f
  emptyprog.f90
  funcsub.f90
  hello.f90
  loop_test.f90
  trivial.f90
)

# Add a smoke test of the fparse-llvm script
add_test(NAME fparse_llvm_smoke_test
  COMMAND ${CMAKE_BINARY_DIR}/fparse-llvm -h)
set_tests_properties(fparse_llvm_smoke_test
  PROPERTIES
  LABELS smoke
  PASS_REGULAR_EXPRESSION "Usage"
)

foreach(test_source IN LISTS FORTRAN_TESTS_SOURCES_LIST)
  add_test(NAME instrument_${test_source}
    COMMAND ./fparse-llvm ${CMAKE_SOURCE_DIR}/tests/fortran/${test_source}
    )
  set_tests_properties(instrument_${test_source}
    PROPERTIES
    REQUIRED_FILES "${CMAKE_BINARY_DIR}/fparse-llvm"
    ENVIRONMENT "SALT_FORTRAN_CONFIG_FILE=${CMAKE_SOURCE_DIR}/config_files/tau_fortran_config.yaml"
    PASS_REGULAR_EXPRESSION "SALT Instrumentor Plugin finished"
  )
endforeach()

set(fortran_compilers_to_test gfortran flang-new)
set(TAU_F90_OPTS -optVerbose -optLinkOnly)
foreach(compiler IN LISTS fortran_compilers_to_test)
  STRING(TOUPPER ${compiler} upper_comp)
  add_test(NAME setup_${compiler}_dir
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/${upper_comp})
  set_tests_properties(setup_${compiler}_dir
    PROPERTIES
    FIXTURES_SETUP ${upper_comp}_dir
  )
endforeach()

foreach(test_source IN LISTS FORTRAN_TESTS_SOURCES_LIST)
  # Get the name of the instrumented source file
  get_filename_component(TEST_BASE_NAME ${test_source} NAME_WLE)
  get_filename_component(TEST_LANG ${test_source} LAST_EXT)
  # fparse-llvm is adding preprocessor directives and should emit uppercase file extensions (e.g., .F90)
  string(TOUPPER ${TEST_LANG} TEST_LANG)
  set(TEST_INST_SOURCE ${TEST_BASE_NAME}.inst${TEST_LANG})

  foreach(compiler IN LISTS fortran_compilers_to_test)
    STRING(TOUPPER ${compiler} upper_comp)
    if(${compiler} STREQUAL "gfortran")
      set(mapped_comp GCC)
      set(EXTRA_FLAGS -Wpedantic -Wextra -Wno-missing-include-dirs -Werror)
    elseif(${compiler} STREQUAL "flang-new")
      set(mapped_comp CLANG)
      set(EXTRA_FLAGS -Werror)
    elseif(${compiler} STREQUAL "flang")
      set(mapped_comp CLANG)
      set(EXTRA_FLAGS -Werror)
    else()
      message(FATAL_ERROR "Unknown compiler: ${compiler}")
    endif()
    add_test(NAME compile_${upper_comp}_${test_source}
      COMMAND ${TAUF90} ${TAU_F90_OPTS} -o ${TEST_BASE_NAME} -Wall ${EXTRA_FLAGS} ${CMAKE_BINARY_DIR}/${TEST_INST_SOURCE}
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${upper_comp}
      )
    set_tests_properties(compile_${upper_comp}_${test_source}
      PROPERTIES
      ENVIRONMENT "TAU_MAKEFILE=${TAU_${mapped_comp}_MAKEFILE}"
      DEPENDS "instrument_${test_source}"
      FIXTURES_REQUIRED ${upper_comp}_dir
      FAIL_REGULAR_EXPRESSION "[^W][Ee]rror"
    )
    # Profile with TAU and Verify profiles are created
    add_test(NAME run_${upper_comp}_${test_source}
      COMMAND ${TAU_EXEC} -T serial,pthread ./${TEST_BASE_NAME}
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${upper_comp}
    )
    set_tests_properties(run_${upper_comp}_${test_source}
      PROPERTIES
      ENVIRONMENT "TAU_MAKEFILE=${TAU_${mapped_comp}_MAKEFILE};PROFILEDIR=${TEST_BASE_NAME}.d"
      DEPENDS compile_${upper_comp}_${test_source}
      FAIL_REGULAR_EXPRESSION "[Cc]ommand not found;[Ss]egmentation;[Ff]ault;[Ee]rror"
    )
    add_test(NAME check_${upper_comp}_${test_source}_profile
      COMMAND ${CMAKE_COMMAND} -E cat ./${TEST_BASE_NAME}.d/profile.0.0.0
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