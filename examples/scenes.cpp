#include <iostream>
#include <sengine/scene_loader.hpp>

namespace {
struct body {
    float mass{1};
    sengine::entity anchor;
};
}
int main(int argc, char** argv) {
    try {
        sengine::component_schema<body> schema;
        schema.field("mass", &body::mass).field("anchor", &body::anchor);
        sengine::scene_registry registry;
        registry.add("body", std::move(schema));
        sengine::asset_store assets(std::filesystem::absolute(argv[0]).parent_path() / "data");
        sengine::scene_loader loader(registry, assets);
        auto scene = loader.load("scenes/paired.json");
        for (const auto& [name, id] : scene->names())
            if (const auto* value = scene->entities().get<body>(id))
                std::cout << name << " mass=" << value->mass
                          << " anchor=" << scene->entities().name(value->anchor) << '\n';
        if (argc > 1)
            sengine::save_scene(argv[1], loader.capture(*scene));
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
