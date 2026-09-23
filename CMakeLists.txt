cmake_minimum_required(VERSION 3.24)
project(sengine VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(sengine_graphics "Build window, audio and rendering services" ON)
set(sengine_window_backend sdl CACHE STRING "Window backend: sdl or glfw")
set_property(CACHE sengine_window_backend PROPERTY STRINGS sdl glfw)
if(APPLE)
  set(default_graphics metal)
else()
  set(default_graphics opengl)
endif()
set(sengine_graphics_api ${default_graphics} CACHE STRING "Graphics API: opengl, vulkan or metal")
set_property(CACHE sengine_graphics_api PROPERTY STRINGS opengl vulkan metal)
add_library(sengine
  src/animation.cpp
  src/assets.cpp
  src/camera_path.cpp
  src/geometry.cpp
  src/input.cpp
  src/jobs.cpp
  src/math.cpp
  src/primitives.cpp
  src/timing.cpp
  src/world.cpp)
add_library(sengine::sengine ALIAS sengine)
target_compile_features(sengine PUBLIC cxx_std_23)
find_package(Threads REQUIRED)
target_link_libraries(sengine PRIVATE Threads::Threads)
target_include_directories(sengine PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
set_target_properties(sengine PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
  target_compile_options(sengine PRIVATE /W4 /permissive-)
else()
  target_compile_options(sengine PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(sengine_graphics)
  if(NOT sengine_window_backend MATCHES "^(sdl|glfw)$" OR NOT sengine_graphics_api MATCHES "^(opengl|vulkan|metal)$")
    message(FATAL_ERROR "Unsupported sengine backend")
  endif()
  include(cmake/filament.cmake)
  find_package(SDL3 3.2 REQUIRED CONFIG)
  find_package(Freetype REQUIRED)
  find_package(PNG REQUIRED)
  target_sources(sengine PRIVATE src/native_canvas.cpp src/image_export.cpp src/platform.cpp
    src/backends/${sengine_window_backend}_window.cpp src/backends/sdl_audio.cpp
    src/backends/filament_renderer.cpp src/backends/filament_hud.cpp src/backends/font_atlas.cpp
    src/backends/filament_nodes.cpp src/backends/filament_lights.cpp src/backends/filament_scene.cpp src/backends/filament_materials.cpp
    src/backends/filament_meshes.cpp src/backends/filament_animation.cpp src/backends/filament_environment.cpp)
  target_include_directories(sengine PRIVATE src)
  target_compile_definitions(sengine PRIVATE sengine_default_graphics=${sengine_graphics_api})
  target_link_libraries(sengine PRIVATE sengine::filament SDL3::SDL3 Threads::Threads Freetype::Freetype PNG::PNG)
  if(sengine_window_backend STREQUAL "glfw")
    find_package(glfw3 3.3 REQUIRED)
    target_link_libraries(sengine PRIVATE glfw)
    if(APPLE)
      enable_language(OBJCXX)
      target_sources(sengine PRIVATE src/backends/metal_surface.mm)
    endif()
  endif()
  if(PROJECT_IS_TOP_LEVEL)
    add_executable(sengine_viewer examples/viewer.cpp)
    target_link_libraries(sengine_viewer PRIVATE sengine)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/materials/surface.filamat"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/materials"
      COMMAND "${filament_root}/bin/matc" -a all -p desktop
        -o "${CMAKE_CURRENT_BINARY_DIR}/materials/surface.filamat"
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/materials/surface.mat"
      DEPENDS examples/materials/surface.mat VERBATIM)
    add_executable(sengine_gallery examples/gallery.cpp "${CMAKE_CURRENT_BINARY_DIR}/materials/surface.filamat")
    target_link_libraries(sengine_gallery PRIVATE sengine)
  endif()
endif()

if(PROJECT_IS_TOP_LEVEL)
  add_executable(sengine_orbit examples/orbit.cpp)
  target_link_libraries(sengine_orbit PRIVATE sengine)
  file(COPY examples/data DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
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
