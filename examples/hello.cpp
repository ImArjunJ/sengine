#include "run.hpp"
#include <sengine/primitives.hpp>

class hello : public sengine::game_scene {
  public:
    explicit hello(sengine::application& host) : game_scene(host) {
        create_mesh(sengine::box_mesh(), surface({.color = {.25f, .65f, .8f, 1}}));
    }
};

int main(int argc, char** argv) {
    return example::run<hello>("sengine / hello", argc, argv);
}
