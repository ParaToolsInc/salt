# SALTGitHooks.cmake -- Point the local clone at the vendored .githooks/ dir.
#
# Sets `core.hooksPath` to a path relative to the repo root so hooks committed
# to .githooks/ are picked up automatically. This is per-clone; it does not
# touch any existing files in .git/hooks/, and it is harmless if the dev
# already maintains their own hooks elsewhere (the new path simply takes
# precedence).
#
# Conditions to skip:
#   - Git not found
#   - Source tree is not a git checkout (e.g. extracted tarball)
#   - .githooks/ does not exist (defensive)
#   - core.hooksPath is already set to the desired value
#   - SALT_INSTALL_GIT_HOOKS=OFF (escape hatch)

option(SALT_INSTALL_GIT_HOOKS
  "If ON, point this clone's git hooks at the vendored .githooks/ directory."
  ON)

if(NOT SALT_INSTALL_GIT_HOOKS)
  return()
endif()

find_package(Git QUIET)
if(NOT GIT_FOUND)
  return()
endif()

set(_salt_repo_root "${CMAKE_CURRENT_LIST_DIR}/../..")
get_filename_component(_salt_repo_root "${_salt_repo_root}" ABSOLUTE)

if(NOT EXISTS "${_salt_repo_root}/.git")
  return()
endif()
if(NOT IS_DIRECTORY "${_salt_repo_root}/.githooks")
  return()
endif()

execute_process(
  COMMAND "${GIT_EXECUTABLE}" config --local --get core.hooksPath
  WORKING_DIRECTORY "${_salt_repo_root}"
  OUTPUT_VARIABLE _salt_current_hooks_path
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
  RESULT_VARIABLE _salt_get_rc)

set(_salt_desired_hooks_path ".githooks")

if(_salt_get_rc EQUAL 0 AND _salt_current_hooks_path STREQUAL _salt_desired_hooks_path)
  return()
endif()

execute_process(
  COMMAND "${GIT_EXECUTABLE}" config --local core.hooksPath "${_salt_desired_hooks_path}"
  WORKING_DIRECTORY "${_salt_repo_root}"
  RESULT_VARIABLE _salt_set_rc
  ERROR_VARIABLE _salt_set_err)

if(_salt_set_rc EQUAL 0)
  message(STATUS "SALT-FM: configured git core.hooksPath = ${_salt_desired_hooks_path}")
else()
  message(WARNING "SALT-FM: failed to set git core.hooksPath: ${_salt_set_err}")
endif()

unset(_salt_repo_root)
unset(_salt_current_hooks_path)
unset(_salt_desired_hooks_path)
unset(_salt_get_rc)
unset(_salt_set_rc)
unset(_salt_set_err)
