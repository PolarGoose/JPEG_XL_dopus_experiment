cmake_minimum_required(VERSION 3.31)
project(JPEG_XL_dopus_experiment LANGUAGES CXX RC)

include(FetchContent)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

set(VERSION_MAJOR 0 CACHE STRING "Major version")
set(VERSION_MINOR 0 CACHE STRING "Minor version")

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBJXL REQUIRED IMPORTED_TARGET libjxl)
pkg_check_modules(LIBJXL_THREADS REQUIRED IMPORTED_TARGET libjxl_threads)

# Download DirectoryOpus Plugin API
FetchContent_Populate(
  directory_opus_plugin_sdk
  URL https://cdn2.gpsoft.com.au/files/Misc/opus_sdk.zip
  URL_HASH MD5=a244c8d64f2211b3bf2774bd8b997c6d)
file(COPY "${directory_opus_plugin_sdk_SOURCE_DIR}/headers/" DESTINATION "${CMAKE_BINARY_DIR}/DirectoryOpus-plugin-SDK/DirectoryOpus-plugin-SDK")

file(GLOB_RECURSE src_files CONFIGURE_DEPENDS "src/*.h" "src/*.cpp")

add_library(jpegxl MODULE ${src_files})
target_include_directories(jpegxl PRIVATE "src/jpegxl" "${CMAKE_BINARY_DIR}/DirectoryOpus-plugin-SDK")
target_compile_features(jpegxl PRIVATE cxx_std_23)
target_compile_options(jpegxl PRIVATE
  /utf-8
  /external:anglebrackets
  /external:W0
  /WX
  /W4
  /analyze
  /analyze:external-)
target_compile_definitions(jpegxl PRIVATE
  UNICODE
  DOPUS_PLUGIN_HELPER
  WIN32_LEAN_AND_MEAN
  VC_EXTRALEAN
  NOMINMAX
  VERSION_MAJOR=${VERSION_MAJOR}
  VERSION_MINOR=${VERSION_MINOR})
target_link_libraries(jpegxl PRIVATE PkgConfig::LIBJXL_THREADS PkgConfig::LIBJXL)
target_precompile_headers(jpegxl PRIVATE "src/jpegxl/PrecompiledHeader.h")

# Workaround for https://github.com/microsoft/vcpkg/issues/44979
add_library(m INTERFACE)
target_link_libraries(jpegxl PRIVATE m)
