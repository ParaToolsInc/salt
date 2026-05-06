# SALTVersion.cmake -- Derive SALT version from .VERSION + git describe.
#
# Outputs (set in PARENT_SCOPE / cache as appropriate):
#   SALT_VERSION_BASE       Numeric MAJOR.MINOR.PATCH for project(VERSION ...)
#   SALT_VERSION_PRERELEASE Pre-release suffix from .VERSION (e.g. -rc1) or empty
#   SALT_VERSION_BUILDMETA  Build metadata derived from git describe or empty
#   SALT_VERSION_FULL       Display string: BASE[PRE][+BUILDMETA]
#   SALT_VERSION_MAJOR/MINOR/PATCH  Convenience integer components
#
# Drift policy:
#   - Latest tag ahead of .VERSION  -> FATAL_ERROR (bump .VERSION before configure)
#   - .VERSION ahead of latest tag  -> STATUS warning (expected before tagging)
#   - .VERSION matches HEAD's tag   -> silent OK
#   - No .git or no git binary      -> use .VERSION verbatim, no build metadata
#   - Missing .VERSION              -> FATAL_ERROR

set(_salt_version_file "${CMAKE_CURRENT_LIST_DIR}/../../.VERSION")

if(NOT EXISTS "${_salt_version_file}")
  message(FATAL_ERROR
    "Cannot determine SALT version: .VERSION file not found at "
    "${_salt_version_file}")
endif()

file(READ "${_salt_version_file}" _salt_version_raw)
string(STRIP "${_salt_version_raw}" _salt_version_raw)

# SemVer 2.0 split: MAJOR.MINOR.PATCH[-PRERELEASE][+BUILDMETA]
# .VERSION should not carry +BUILDMETA (that's git-derived); reject if present.
if(_salt_version_raw MATCHES "\\+")
  message(FATAL_ERROR
    ".VERSION must not contain '+BUILDMETA' (got '${_salt_version_raw}'). "
    "Build metadata is derived from git describe at configure time.")
endif()

if(_salt_version_raw MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-[0-9A-Za-z.-]+)?$")
  set(SALT_VERSION_MAJOR "${CMAKE_MATCH_1}")
  set(SALT_VERSION_MINOR "${CMAKE_MATCH_2}")
  set(SALT_VERSION_PATCH "${CMAKE_MATCH_3}")
  set(SALT_VERSION_BASE "${SALT_VERSION_MAJOR}.${SALT_VERSION_MINOR}.${SALT_VERSION_PATCH}")
  set(SALT_VERSION_PRERELEASE "${CMAKE_MATCH_4}")
else()
  message(FATAL_ERROR
    ".VERSION ('${_salt_version_raw}') is not valid SemVer "
    "(expected MAJOR.MINOR.PATCH[-PRERELEASE]).")
endif()

set(SALT_VERSION_BUILDMETA "")

# git describe overlay (optional)
find_package(Git QUIET)
set(_salt_git_dir "${CMAKE_CURRENT_LIST_DIR}/../../.git")
if(GIT_FOUND AND EXISTS "${_salt_git_dir}")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" describe --tags --long --dirty --always
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../.."
    OUTPUT_VARIABLE _salt_git_desc
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _salt_git_rc)

  if(_salt_git_rc EQUAL 0 AND _salt_git_desc)
    # Forms:
    #   v0.3.0-5-g0ab8b89[-dirty]    (tagged repo, post-tag commits)
    #   v0.3.0-0-g0ab8b89[-dirty]    (exact tag)
    #   0ab8b89[-dirty]              (--always fallback, no tags reachable)
    set(_salt_desc_re
      "^v?([0-9]+\\.[0-9]+\\.[0-9]+(-[0-9A-Za-z.-]+)?)-([0-9]+)-g([0-9a-f]+)(-dirty)?$")
    if(_salt_git_desc MATCHES "${_salt_desc_re}")
      set(_tag_ver "${CMAKE_MATCH_1}")
      set(_commits_ahead "${CMAKE_MATCH_3}")
      set(_short_sha "${CMAKE_MATCH_4}")
      set(_dirty_flag "${CMAKE_MATCH_5}")

      # Drift checks: compare tag version to .VERSION
      if(NOT _tag_ver STREQUAL _salt_version_raw)
        # Determine which is ahead. Numeric BASE comparison is sufficient for
        # FATAL detection; pre-release ordering nuances we report as warnings.
        string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" _tag_base_match "${_tag_ver}")
        set(_tag_major ${CMAKE_MATCH_1})
        set(_tag_minor ${CMAKE_MATCH_2})
        set(_tag_patch ${CMAKE_MATCH_3})
        set(_tag_base_numeric "${_tag_major}.${_tag_minor}.${_tag_patch}")

        if(_tag_base_numeric VERSION_GREATER SALT_VERSION_BASE)
          message(FATAL_ERROR
            ".VERSION (${_salt_version_raw}) lags behind latest reachable tag "
            "(v${_tag_ver}). Bump .VERSION to >= ${_tag_base_numeric} before configuring.")
        elseif(_tag_base_numeric VERSION_LESS SALT_VERSION_BASE)
          message(STATUS
            "SALT version: .VERSION (${_salt_version_raw}) is ahead of latest "
            "reachable tag (v${_tag_ver}); release tag not yet created.")
        else()
          # BASE matches; pre-release component differs.
          message(STATUS
            "SALT version: .VERSION (${_salt_version_raw}) base matches tag "
            "(v${_tag_ver}) but pre-release suffixes differ.")
        endif()
      endif()

      if(_commits_ahead GREATER 0 OR _dirty_flag)
        set(SALT_VERSION_BUILDMETA "+${_commits_ahead}.g${_short_sha}")
        if(_dirty_flag)
          string(APPEND SALT_VERSION_BUILDMETA ".dirty")
        endif()
      endif()
    elseif(_salt_git_desc MATCHES "^([0-9a-f]+)(-dirty)?$")
      # No tags reachable -- include sha only.
      set(SALT_VERSION_BUILDMETA "+g${CMAKE_MATCH_1}")
      if(CMAKE_MATCH_2)
        string(APPEND SALT_VERSION_BUILDMETA ".dirty")
      endif()
      message(STATUS "SALT version: no reachable tag; using .VERSION verbatim with build metadata.")
    else()
      message(WARNING "Unrecognized git describe output: '${_salt_git_desc}'")
    endif()
  endif()
endif()

set(SALT_VERSION_FULL "${SALT_VERSION_BASE}${SALT_VERSION_PRERELEASE}${SALT_VERSION_BUILDMETA}")

unset(_salt_desc_re)
unset(_salt_version_file)
unset(_salt_version_raw)
unset(_salt_git_dir)
unset(_salt_git_desc)
unset(_salt_git_rc)
unset(_tag_ver)
unset(_commits_ahead)
unset(_short_sha)
unset(_dirty_flag)
unset(_tag_base_match)
unset(_tag_major)
unset(_tag_minor)
unset(_tag_patch)
unset(_tag_base_numeric)
