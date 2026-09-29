#pragma once
#include <chrono>
#include <iostream>
#include <sengine/game_scene.hpp>

namespace example {
template <std::derived_from<sengine::game_scene> scene> int run(std::string title, int argc, char** argv) {
    try {
        sengine::application host({.title = std::move(title), .width = 1200, .height = 800});
        if (argc < 2) {
            host.run<scene>();
            return 0;
        }
        auto& content = host.scenes().emplace<scene>(host);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        for (unsigned frame = 0; frame < 120 || content.capture_pending(); ++frame) {
            if (std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error("Example capture timed out");
            host.scenes().begin_frame();
            host.scenes().advance(frame < 120 ? 1.0 / 60 : 0);
            if (frame == 119)
                content.capture(argv[1]);
            host.scenes().render();
            sengine::sleep_for(1.0 / 120);
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
}
