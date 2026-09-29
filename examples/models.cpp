#include "run.hpp"
#include <sengine/primitives.hpp>

using namespace sengine;

class models : public game_scene {
  public:
    explicit models(application& host) : game_scene(host) {
        camera({.eye = {5, 4.5f, 8}, .direction = {-5, -3.5f, -8}, .fov = 45});
        sun({.color = {1, .94f, .85f}, .intensity = 18000, .direction = {-.4f, -1, -.6f}});
        environment({.intensity = 14000});
        auto floor = create_mesh(box_mesh({4, .1f, 2.5f}), surface({.color = {.2f, .27f, .32f, 1}}));
        floor.position({0, -.1f, 0});
        left_ = create_model("data/models/mobile.gltf");
        left_.position({-1.7f, 0, 0});
        left_.play("Turn");
        auto right = create_model("data/models/mobile.gltf");
        right.position({1.7f, 0, 0});
        right.play("Sway");
        bind("animate", {{.key = key_code::space}});
        bind("reload", {{.key = key_code::r}});
        bind("quit", {{.key = key_code::escape}});
    }
    void update(const runtime_frame&) override {
        if (input().action("animate").pressed)
            left_.play(left_.animation() == "Turn" ? "Sway" : "Turn", .5);
        if (input().action("reload").pressed)
            left_.reload();
        if (input().action("quit").pressed)
            quit();
    }

  private:
    game_object left_;
};

int main(int argc, char** argv) {
    return example::run<models>("sengine / models", argc, argv);
}
