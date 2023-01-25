#Print an error message on an attempt to build at the top level of the source tree:
include_guard(GLOBAL)
macro(check_out_of_source_build)
  if ("${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_BINARY_DIR}")
    message(FATAL_ERROR "ERROR! "
      "CMAKE_CURRENT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
      " == CMAKE_CURRENT_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
      "\nThis archive does not support in-source builds:\n"
      "You must now delete the CMakeCache.txt file and the CMakeFiles/ directory under "
      "the 'src' source directory or you will not be able to configure correctly!"
      "\nYou must now run something like:\n"
      "  $ rm -r CMakeCache.txt CMakeFiles/"
      "\n"
      "Please create a directory outside the 'FAST' source tree and build under that outside directory "
      "in a manner such as\n"
      "  $ mkdir build\n"
      "  $ cd build\n"
      "  $ FC=gfortran cmake <path-to-FAST-source-directory> -DCMAKE_INSTALL_PREFIX=<path-to-install-directory>\n"
      "\nsubstituting the appropriate syntax for your shell (the above line assumes the bash shell)."
      )
  endif()
endmacro()
