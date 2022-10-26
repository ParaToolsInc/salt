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

# Set the LLVM components needed by salt
set(LLVM_NEEDED_COMPONENTS
  all
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
message(STATUS "LLVM_TOOLS_BINARY_DIR:\n\t${LLVM_TOOLS_BINARY_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})

list(LENGTH LLVM_INCLUDE_DIRS NUM_CLANG_HEADER_INCLUDES)
list(JOIN LLVM_INCLUDE_DIRS "\", \"-I" CLANG_HEADER_INCLUDES)
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
set(SYSTEM_LIBS -lrt -ldl -lpthread -lm)
set(CLANG_LIBS
  -lclangFrontend
  -lclangSerialization
  -lclangDriver
  -lclangTooling
  -lclangParse
  -lclangSema
  -lclangAnalysis
  -lclangEdit
  -lclangAST
  -lclangLex
  -lclangBasic
  -lclangRewrite
  -lclangRewriteFrontend
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
target_compile_options(SALT_LLVM_TOOLING INTERFACE -std=c++14 -fno-rtti -Wall -Werror -Wpedantic)
target_link_libraries(SALT_LLVM_TOOLING INTERFACE ${CLANG_LIBS} ${LLVM_LIBS} ${SYSTEM_LIBS})

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
target_link_options(cparse-llvm PUBLIC -Wl,--as-needed -Wl,--no-allow-shlib-undefined -Wl,--no-undefined)
