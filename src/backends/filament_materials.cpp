#include "backends/filament_scene_state.hpp"
#include "backends/filament_values.hpp"
#include "backends/material_package.hpp"
namespace sengine {
using namespace filament_detail;
material_id material_at(scene& s, scene_node n, unsigned slot) {
    auto& p = scene_data(s);
    auto& rm = p.engine.getRenderableManager();
    auto* material =
        const_cast<filament::MaterialInstance*>(rm.getMaterialInstanceAt(rm.getInstance(native(n)), slot));
    p.materials.push_back({material, false});
    return {p.materials.size() - 1};
}
material_id load_material(scene& s, const std::filesystem::path& path) {
    auto& p = scene_data(s);
    auto data = bytes(path);
    try {
        validate_material_package(std::as_bytes(std::span(data)), p.engine.getBackend());
    } catch (const std::exception& error) {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
    auto* shader = filament::Material::Builder().package(data.data(), data.size()).build(p.engine);
    if (!shader)
        throw std::runtime_error("Invalid material: " + path.string());
    try {
        p.shaders.reserve(p.shaders.size() + 1);
        p.materials.reserve(p.materials.size() + 1);
        auto* instance = shader->createInstance();
        if (!instance)
            throw std::runtime_error("Cannot create material instance");
        p.shaders.push_back(shader);
        p.materials.push_back({instance, true});
    } catch (...) {
        p.engine.destroy(shader);
        throw;
    }
    return {p.materials.size() - 1};
}
material_id duplicate_material(scene& s, material_id source, const std::string& name) {
    auto& p = scene_data(s);
    p.materials.reserve(p.materials.size() + 1);
    auto* material =
        filament::MaterialInstance::duplicate(p.materials.at(source.value).material, name.c_str());
    if (!material)
        throw std::runtime_error("Cannot duplicate material");
    p.materials.push_back({material, true});
    return {p.materials.size() - 1};
}
void set_material(scene& s, scene_node n, material_id material, unsigned slot) {
    auto& p = scene_data(s);
    auto& rm = p.engine.getRenderableManager();
    rm.setMaterialInstanceAt(rm.getInstance(native(n)), slot, p.materials.at(material.value).material);
}
void set_parameter(scene& s, material_id m, const std::string& name, float v) {
    scene_data(s).materials.at(m.value).material->setParameter(name.c_str(), v);
}
void set_color(scene& s, material_id m, const std::string& name, float4 v, bool srgb) {
    scene_data(s).materials.at(m.value).material->setParameter(
        name.c_str(), srgb ? filament::RgbaType::sRGB : filament::RgbaType::LINEAR, native(v));
}
}
