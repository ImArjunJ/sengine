#include <iostream>
#include <sengine/physics.hpp>

int main() {
    try {
        sengine::world world;
        const auto floor = world.create("floor");
        world.set_local_transform(floor, sengine::translation({0, -.5f, 0}));
        world.emplace<sengine::rigid_body>(floor, sengine::rigid_body{.half_extent = {5, .5f, 5}});
        const auto box = world.create("box");
        world.set_local_transform(box, sengine::translation({0, 4, 0}));
        world.emplace<sengine::rigid_body>(box, sengine::rigid_body{.motion = sengine::body_motion::dynamic});
        sengine::physics_world physics(world);
        for (unsigned step = 0; step < 180; ++step) {
            physics.step();
            for (const auto& contact : physics.events())
                if (contact.phase == sengine::collision_phase::begin)
                    std::cout << world.name(contact.first) << " met " << world.name(contact.second) << '\n';
        }
        std::cout << "box height: " << world.world_transform(box)[3].y << '\n';
        if (const auto hit = physics.raycast({0, 10, 0}, {0, -20, 0}))
            std::cout << "ray hit: " << world.name(hit->target) << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
