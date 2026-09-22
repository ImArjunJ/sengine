# sengine

C++23 tools for windows, input, audio, scenes and drawing. Backend details stay inside the engine.

![sengine example](.github/path.png)

```sh
./tools/fetch_filament.sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/clang_libcxx.cmake
cmake --build build
./build/sengine_viewer scene.gltf
```

Choose `sengine_window_backend=sdl` or `glfw`. Rendering uses `sengine_graphics_api=opengl` or `vulkan` on Linux, and `metal` on macOS. Defaults are SDL and the native graphics API. Filament handles rendering; SDL handles audio.

Needs Clang with libc++, SDL3 3.2+, FreeType and libpng. GLFW builds also need GLFW 3.3+. Use `filament_root` for an existing Filament 1.77.1 SDK.

Link `sengine::sengine` through `add_subdirectory` or an installed CMake package. Public headers use engine types and the standard library. Set `sengine_graphics=OFF` for geometry, math and cameras alone. CMake exports compile commands automatically.
