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
      "\nThis project does not support in-source builds:\n"
      "Delete the CMakeCache.txt file and the CMakeFiles/ directory "
      "in ${CMAKE_CURRENT_SOURCE_DIR} before you can configure correctly."
      "\nYou must now run something like:\n"
      "  $ rm -r ${CMAKE_CURRENT_SOURCE_DIR}/CMakeCache.txt "
      "${CMAKE_CURRENT_SOURCE_DIR}/CMakeFiles/"
      "\n"
      "Then create a build directory outside the source tree and "
      "configure from there, for example:\n"
      "  $ mkdir build\n"
      "  $ cd build\n"
      "  $ FC=gfortran cmake ${CMAKE_CURRENT_SOURCE_DIR} "
      "-DCMAKE_INSTALL_PREFIX=<path-to-install-directory>\n"
      "\nsubstituting the appropriate syntax for your shell "
      "(the above line assumes the bash shell).")
  endif()
endfunction()
