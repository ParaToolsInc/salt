include_guard(GLOBAL)

# Print a FATAL_ERROR if the current configure is happening at the top of
# the source tree (an in-source build). Salt does not support in-source
# builds because they pollute the source tree with CMake artifacts and
# break clean checkouts; configure from a dedicated build directory.
function(check_out_of_source_build)
  if("${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_BINARY_DIR}")
    message(FATAL_ERROR
      "ERROR! "
      "CMAKE_CURRENT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
      " == CMAKE_CURRENT_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
      "\nThis archive does not support in-source builds:\n"
      "You must now delete the CMakeCache.txt file and the CMakeFiles/ "
      "directory under the 'src' source directory or you will not be "
      "able to configure correctly!"
      "\nYou must now run something like:\n"
      "  $ rm -r CMakeCache.txt CMakeFiles/"
      "\n"
      "Please create a directory outside the 'salt' source tree and "
      "build under that outside directory in a manner such as\n"
      "  $ mkdir build\n"
      "  $ cd build\n"
      "  $ FC=gfortran cmake <path-to-salt-source-directory> "
      "-DCMAKE_INSTALL_PREFIX=<path-to-install-directory>\n"
      "\nsubstituting the appropriate syntax for your shell "
      "(the above line assumes the bash shell).")
  endif()
endfunction()
