#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "HiFace::HiFace" for configuration "Release"
set_property(TARGET HiFace::HiFace APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(HiFace::HiFace PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libhiface.so.0.8.5"
  IMPORTED_SONAME_RELEASE "libhiface.so.1"
  )

list(APPEND _cmake_import_check_targets HiFace::HiFace )
list(APPEND _cmake_import_check_files_for_HiFace::HiFace "${_IMPORT_PREFIX}/lib/libhiface.so.0.8.5" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
