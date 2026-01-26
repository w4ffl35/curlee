include(FindPackageHandleStandardArgs)

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_Z3 QUIET z3)
endif()

find_path(Z3_INCLUDE_DIR NAMES z3.h HINTS ${PC_Z3_INCLUDE_DIRS})
find_library(Z3_LIBRARY NAMES z3 libz3 HINTS ${PC_Z3_LIBRARY_DIRS})

find_package_handle_standard_args(Z3 DEFAULT_MSG Z3_LIBRARY Z3_INCLUDE_DIR)

if(Z3_FOUND)
  set(Z3_INCLUDE_DIRS ${Z3_INCLUDE_DIR})
  set(Z3_LIBRARIES ${Z3_LIBRARY})

  if(NOT TARGET Z3::Z3)
    add_library(Z3::Z3 UNKNOWN IMPORTED)
    set_target_properties(Z3::Z3 PROPERTIES
      IMPORTED_LOCATION "${Z3_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${Z3_INCLUDE_DIR}"
    )
  endif()
endif()
