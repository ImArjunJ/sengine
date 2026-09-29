#include "run.hpp"
#include <sengine/primitives.hpp>

using namespace sengine;

class playground : public game_scene {
  public:
    explicit playground(application& host) : game_scene(host) {
        camera({.eye = {8, 10, 12}, .direction = {-8, -9, -12}, .fov = 48});
        enable_physics();
        auto floor = create_mesh(box_mesh({6, .3f, 5}), surface({.color = {.16f, .23f, .29f, 1}}));
        floor.position({0, -.3f, 0});
        floor.add<rigid_body>({.half_extent = {6, .3f, 5}});
        const auto cube = upload(box_mesh());
        const auto paint = surface({.color = {.81f, .32f, .27f, 1}});
        for (unsigned i = 0; i < 3; ++i) {
            auto box = create_mesh(cube, paint);
            box.position({-1, 1 + i * 1.4f, 0});
            box.add<rigid_body>({.motion = body_motion::dynamic});
        }
        player_ = create_mesh(sphere_mesh(.4f), surface({.color = {.22f, .8f, .87f, 1}}));
        player_.position({0, 1, 3});
        player_.add<character_body>({.radius = .4f, .half_height = 0});
        bind("horizontal", {{.key = key_code::a, .scale = -1}, {.key = key_code::d}});
        bind("vertical", {{.key = key_code::w, .scale = -1}, {.key = key_code::s}});
        bind("jump", {{.key = key_code::space}});
        bind("quit", {{.key = key_code::escape}});
    }
    void fixed_update(runtime_step) override {
        const auto direction = fixed_input().axis_pair("horizontal", "vertical");
        player_.walk({direction[0] * 3, direction[1] * 3}, fixed_input().action("jump").pressed);
    }
    void update(const runtime_frame&) override {
        if (input().action("quit").pressed)
            quit();
    }

  private:
    game_object player_;
};

int main(int argc, char** argv) {
    return example::run<playground>("sengine / physics", argc, argv);
}
