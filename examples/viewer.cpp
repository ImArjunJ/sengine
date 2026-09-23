#include <chrono>
#include <iostream>
#include <sengine/renderer.hpp>
#include <sengine/scene.hpp>
int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "viewer scene.gltf [capture.png]\n";
        return 0;
    }
    try {
        sengine::window window("sengine viewer", 960, 640);
        sengine::renderer renderer(window);
        sengine::scene scene(renderer);
        renderer.configure({});
        sengine::load_scene(scene, argv[1]);
        sengine::add_sun(scene, {});
        sengine::set_environment(scene, {});
        sengine::camera_view camera{.eye = {0, 1.5f, 4}, .direction = {0, -.1f, -1}};
        const auto start = std::chrono::steady_clock::now();
        unsigned frames = 0;
        while (true) {
            sengine::input_event event;
            while (window.poll(event))
                if (event.type == sengine::event_type::close || event.type == sengine::event_type::quit ||
                    (event.type == sengine::event_type::key_down &&
                     event.key.code == sengine::key_code::escape))
                    return 0;
            const auto size = window.metrics();
            const auto capture =
                argc > 2 && frames >= 10 ? std::filesystem::path(argv[2]) : std::filesystem::path{};
            if (renderer.frame(camera, size.width, size.height, .05f, 330, nullptr, capture)) {
                ++frames;
                if (!capture.empty())
                    return 0;
            }
            if (argc > 2 && std::chrono::steady_clock::now() - start > std::chrono::seconds(60))
                return 2;
            sengine::sleep_for(1.0 / 60);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
