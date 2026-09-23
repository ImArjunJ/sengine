cmake_minimum_required(VERSION 3.24)
project(sengine VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(sengine_graphics "Build window, audio and rendering services" ON)
option(sengine_physics "Build physics and collision services" ON)
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
  src/runtime.cpp
  src/scene_document.cpp
  src/scene_loader.cpp
  src/scene_schema.cpp
  src/timing.cpp
  src/world.cpp)
add_library(sengine::sengine ALIAS sengine)
target_compile_features(sengine PUBLIC cxx_std_23)
find_package(Threads REQUIRED)
target_link_libraries(sengine PRIVATE Threads::Threads)
target_include_directories(sengine PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_include_directories(sengine SYSTEM PRIVATE third_party)
set_target_properties(sengine PROPERTIES CXX_EXTENSIONS OFF)
if(sengine_physics)
  include(cmake/physics.cmake)
  target_sources(sengine PRIVATE src/physics.cpp src/physics_schema.cpp)
  target_link_libraries(sengine PRIVATE Jolt)
endif()
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
  target_sources(sengine PRIVATE src/application.cpp src/native_canvas.cpp src/image_export.cpp src/platform.cpp src/world_renderer.cpp
    src/backends/${sengine_window_backend}_window.cpp src/backends/sdl_audio.cpp
    src/backends/filament_renderer.cpp src/backends/filament_hud.cpp src/backends/font_atlas.cpp
    src/backends/filament_nodes.cpp src/backends/filament_lights.cpp src/backends/filament_scene.cpp src/backends/filament_materials.cpp
    src/backends/filament_meshes.cpp src/backends/filament_animation.cpp src/backends/filament_environment.cpp
    src/backends/material_package.cpp)
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
    foreach(material IN ITEMS surface hud)
      add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/materials/${material}.filamat"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/materials"
        COMMAND "${filament_root}/bin/matc" -a all -p desktop
          -o "${CMAKE_CURRENT_BINARY_DIR}/materials/${material}.filamat"
          "${CMAKE_CURRENT_SOURCE_DIR}/examples/materials/${material}.mat"
        DEPENDS "examples/materials/${material}.mat" VERBATIM)
    endforeach()
    add_custom_target(sengine_example_materials DEPENDS
      "${CMAKE_CURRENT_BINARY_DIR}/materials/surface.filamat"
      "${CMAKE_CURRENT_BINARY_DIR}/materials/hud.filamat")
    add_executable(sengine_gallery examples/gallery.cpp)
    add_dependencies(sengine_gallery sengine_example_materials)
    target_link_libraries(sengine_gallery PRIVATE sengine)
    if(sengine_physics)
      add_executable(sengine_physics_demo examples/physics.cpp)
      add_dependencies(sengine_physics_demo sengine_example_materials)
      target_link_libraries(sengine_physics_demo PRIVATE sengine)
    endif()
    add_executable(sengine_relay examples/relay.cpp)
    add_dependencies(sengine_relay sengine_example_materials)
    target_link_libraries(sengine_relay PRIVATE sengine)
  endif()
endif()

if(PROJECT_IS_TOP_LEVEL)
  if(sengine_physics)
    add_executable(sengine_falling examples/falling.cpp)
    target_link_libraries(sengine_falling PRIVATE sengine)
  endif()
  add_executable(sengine_orbit examples/orbit.cpp)
  target_link_libraries(sengine_orbit PRIVATE sengine)
  add_executable(sengine_scenes examples/scenes.cpp)
  target_link_libraries(sengine_scenes PRIVATE sengine)
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
install(FILES third_party/nlohmann/LICENSE.MIT DESTINATION ${CMAKE_INSTALL_DATADIR}/sengine/licenses/nlohmann-json)
install(EXPORT sengine-targets NAMESPACE sengine:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sengine)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/sengine-config.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/sengine-config-version.cmake" cmake/filament.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/sengine)
