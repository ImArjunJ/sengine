# sengine

A C++23 game engine with a backend-neutral API.

![physics](.github/physics.png)

- Scene lifecycles, fixed updates, input routing and frame pacing.
- Entity worlds, scene files, prefabs and synchronized meshes.
- Rigid bodies, characters, collision layers, triggers and spatial queries.
- Jobs, typed assets, asynchronous loading and reloads.
- Animation, procedural geometry, glTF scenes, PBR rendering and audio.

```sh
./tools/fetch_filament.sh
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/clang_libcxx.cmake
cmake --build build
./build/sengine_physics_demo
```

WASD moves, Space jumps, R reloads. Edit `build/data/physics` while running. [Physics](examples/physics.cpp) loads a room and falling-box prefabs; [Relay](examples/relay.cpp) has two small levels.

Choose `sengine_window_backend=sdl` or `glfw`, and `sengine_graphics_api=opengl`, `vulkan` or `metal`. Defaults are SDL and OpenGL on Linux, SDL and Metal on macOS. Rendering uses Filament, audio uses SDL, physics uses Jolt. Public headers expose none of them.

Needs Clang with libc++, SDL3 3.2+, FreeType and libpng; GLFW builds also need GLFW 3.3+. Set `filament_root` for an existing Filament 1.77.1 SDK. CMake fetches Jolt 5.6.0; `sengine_physics=OFF` excludes it.

For the core alone:

```sh
cmake -S . -B build-core -Dsengine_graphics=OFF
cmake --build build-core
./build-core/sengine_falling
./build-core/sengine_scenes
```

Link `sengine::sengine` through `add_subdirectory` or an installed CMake package. CMake exports compile commands.

Scenes use versioned JSON and explicit [component schemas](examples/scenes.cpp). Prefab references are local; `/` references the root scene. `capture()` saves a standalone snapshot. Load replacements before releasing the current scene.

Call `physics_world::step()` at its configured fixed interval. Bodies follow world entities; `synchronize()` applies edits before queries. Characters use Y-up gravity. Spheres and capsules need uniform scale; physics rejects shear and reflection.

Keep borrowed windows, renderers, scenes and worlds alive through their dependents. World changes and physics calls stay on the owning thread. Scene transitions apply next frame; `fixed_input()` retains taps until a fixed update.
