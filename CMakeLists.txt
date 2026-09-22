cmake_minimum_required(VERSION 3.24)
project(sengine VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(sengine_graphics "Build SDL3 and Filament drawing services" ON)
add_library(sengine src/geometry.cpp src/explorer.cpp src/camera_path.cpp)
add_library(sengine::sengine ALIAS sengine)
target_compile_features(sengine PUBLIC cxx_std_23)
target_include_directories(sengine PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
set_target_properties(sengine PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
  target_compile_options(sengine PRIVATE /W4 /permissive-)
else()
  target_compile_options(sengine PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(sengine_graphics)
  include(cmake/filament.cmake)
  find_package(Freetype REQUIRED)
  find_package(PNG REQUIRED)
  target_sources(sengine PRIVATE src/native_hud.cpp src/native_canvas.cpp src/image_export.cpp src/window.cpp src/renderer.cpp)
  target_link_libraries(sengine PUBLIC sengine::filament SDL3::SDL3 Threads::Threads PRIVATE Freetype::Freetype PNG::PNG)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
configure_package_config_file(cmake/sengine-config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/sengine-config.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/sengine")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/sengine-config-version.cmake"
  COMPATIBILITY SameMajorVersion)
install(TARGETS sengine EXPORT sengine-targets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT sengine-targets NAMESPACE sengine:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sengine)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sengine-config.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/sengine-config-version.cmake" cmake/filament.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sengine)
