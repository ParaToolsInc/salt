include_guard(DIRECTORY)

# Collect -D<NAME>[:TYPE]=<VALUE> strings for every cache variable the
# user supplied on the cmake command line (or via an initial cache) and
# return them in CMAKE_ARGS in the caller's scope. Useful for forwarding
# the user's flags into ExternalProject_Add or sub-builds.
function(get_passed_cmake_args)
  set(forwarded "")
  get_cmake_property(cache_vars CACHE_VARIABLES)
  foreach(cache_var ${cache_vars})
    get_property(helpstring CACHE ${cache_var} PROPERTY HELPSTRING)
    if((helpstring STREQUAL "No help, variable specified on the command line.")
       OR (helpstring STREQUAL "Initial cache"))
      get_property(var_type CACHE ${cache_var} PROPERTY TYPE)
      if(var_type STREQUAL "UNINITIALIZED")
        set(type_suffix "")
      else()
        set(type_suffix ":${var_type}")
      endif()
      list(APPEND forwarded "-D${cache_var}${type_suffix}=${${cache_var}}")
    endif()
  endforeach()
  set(CMAKE_ARGS ${forwarded} PARENT_SCOPE)
endfunction()
