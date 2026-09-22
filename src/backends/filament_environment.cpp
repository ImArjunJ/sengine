#include "filament_environment.hpp"
#include <backend/PixelBufferDescriptor.h>
#include <cmath>
#include <filament-iblprefilter/IBLPrefilterContext.h>
#include <filament/Engine.h>
#include <filament/IndirectLight.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/Texture.h>
#include <math/vec3.h>
#include <vector>

namespace sengine {
using namespace filament;
namespace {
math::float3 direction(unsigned face, float u, float v) {
    switch (face) {
    case 0:
        return normalize(math::float3{1, -v, -u});
    case 1:
        return normalize(math::float3{-1, -v, u});
    case 2:
        return normalize(math::float3{u, 1, v});
    case 3:
        return normalize(math::float3{u, -1, -v});
    case 4:
        return normalize(math::float3{u, -v, 1});
    default:
        return normalize(math::float3{-u, -v, -1});
    }
}

math::float3 radiance(math::float3 ray, const environment_options& options) {
    const math::float3 horizon{options.horizon.x, options.horizon.y, options.horizon.z};
    const math::float3 zenith{options.zenith.x, options.zenith.y, options.zenith.z};
    const math::float3 ground{options.ground.x, options.ground.y, options.ground.z};
    const float t = std::pow(std::abs(ray.y), ray.y >= 0 ? options.upper_curve : options.lower_curve);
    return horizon * (1 - t) + (ray.y >= 0 ? zenith : ground) * t;
}

Texture* filter_target(Engine& engine, unsigned size, uint8_t levels) {
    return Texture::Builder()
        .width(size)
        .height(size)
        .levels(levels)
        .usage(Texture::Usage::SAMPLEABLE | Texture::Usage::COLOR_ATTACHMENT)
        .sampler(Texture::Sampler::SAMPLER_CUBEMAP)
        .format(Texture::InternalFormat::R11F_G11F_B10F)
        .build(engine);
}
}

struct filament_environment::impl {
    Engine& engine;
    Scene& scene;
    Texture* dome{};
    Texture* reflections{};
    Texture* irradiance{};
    IndirectLight* light{};
    Skybox* sky{};

    impl(Engine& e, Scene& s) : engine(e), scene(s) {}
    ~impl() {
        scene.setIndirectLight(nullptr);
        scene.setSkybox(nullptr);
        if (light)
            engine.destroy(light);
        if (sky)
            engine.destroy(sky);
        for (auto* texture : {dome, reflections, irradiance})
            if (texture)
                engine.destroy(texture);
    }
};

filament_environment::filament_environment(Engine& engine, Scene& scene, const environment_options& options)
    : impl_(std::make_unique<impl>(engine, scene)) {
    auto& p = *impl_;
    constexpr unsigned size = 64;
    auto pixels = std::make_unique<std::vector<float>>(size * size * 6 * 4);
    for (unsigned face = 0; face < 6; ++face)
        for (unsigned y = 0; y < size; ++y)
            for (unsigned x = 0; x < size; ++x) {
                const auto color = radiance(
                    direction(face, (float(x) + .5f) / size * 2 - 1, (float(y) + .5f) / size * 2 - 1),
                    options);
                const size_t i = ((face * size + y) * size + x) * 4;
                (*pixels)[i] = color.r;
                (*pixels)[i + 1] = color.g;
                (*pixels)[i + 2] = color.b;
                (*pixels)[i + 3] = 1;
            }
    p.dome = Texture::Builder()
                 .width(size)
                 .height(size)
                 .levels(7)
                 .usage(Texture::Usage::DEFAULT | Texture::Usage::GEN_MIPMAPPABLE)
                 .sampler(Texture::Sampler::SAMPLER_CUBEMAP)
                 .format(Texture::InternalFormat::RGBA16F)
                 .build(engine);
    auto* data = pixels.release();
    p.dome->setImage(engine, 0, 0, 0, 0, size, size, 6,
                     backend::PixelBufferDescriptor(
                         data->data(), data->size() * sizeof(float), backend::PixelDataFormat::RGBA,
                         backend::PixelDataType::FLOAT,
                         [](void*, size_t, void* buffer) { delete static_cast<std::vector<float>*>(buffer); },
                         data));
    p.dome->generateMipmaps(engine);
    p.reflections = filter_target(engine, size, 5);
    p.irradiance = filter_target(engine, 32, 1);
    IBLPrefilterContext context(engine);
    IBLPrefilterContext::SpecularFilter specular(context);
    specular({.generateMipmap = false}, p.dome, p.reflections);
    IBLPrefilterContext::IrradianceFilter diffuse(context);
    diffuse({.generateMipmap = false}, p.dome, p.irradiance);
    p.light = IndirectLight::Builder()
                  .irradiance(p.irradiance)
                  .reflections(p.reflections)
                  .intensity(options.intensity)
                  .build(engine);
    p.sky = Skybox::Builder().environment(p.dome).showSun(true).build(engine);
    scene.setIndirectLight(p.light);
    scene.setSkybox(p.sky);
}

filament_environment::~filament_environment() = default;
}
