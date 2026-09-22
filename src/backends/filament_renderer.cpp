#include "backends/filament_access.hpp"
#include "native_surface.hpp"
#include "sengine/image_export.hpp"
#include "sengine/native_hud.hpp"
#include "sengine/renderer.hpp"
#include <backend/PixelBufferDescriptor.h>
#include <cmath>
#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/SwapChain.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <limits>
#include <stdexcept>
#include <utils/EntityManager.h>
#include <vector>

namespace sengine {
struct renderer::impl {
    filament::Engine* engine{};
    filament::SwapChain* swap{};
    filament::Renderer* renderer{};
    filament::Scene* scene{};
    filament::View* view{};
    filament::Camera* camera{};
    utils::Entity camera_entity{};
    ~impl() {
        if (!engine)
            return;
        engine->flushAndWait();
        if (view)
            engine->destroy(view);
        if (scene)
            engine->destroy(scene);
        if (camera_entity) {
            engine->destroyCameraComponent(camera_entity);
            utils::EntityManager::get().destroy(camera_entity);
        }
        if (renderer)
            engine->destroy(renderer);
        if (swap)
            engine->destroy(swap);
        filament::Engine::destroy(&engine);
    }
};
renderer::renderer(window& display) : impl_(std::make_unique<impl>()) {
    auto* window = native_surface::handle(display);
    auto* shared_context = native_surface::shared_context(display);
    if (!window)
        throw std::invalid_argument("A native window is required");
    auto backend = filament::Engine::Backend::DEFAULT;
    switch (display.graphics()) {
    case graphics_api::opengl:
        backend = filament::Engine::Backend::OPENGL;
        break;
    case graphics_api::vulkan:
        backend = filament::Engine::Backend::VULKAN;
        break;
    case graphics_api::metal:
        backend = filament::Engine::Backend::METAL;
        break;
    default:
        break;
    }
    auto& p = *impl_;
    filament::Engine::Config config;
    config.driverHandleArenaSizeMB = 16;
    p.engine = filament::Engine::create(backend, nullptr, shared_context, &config);
    if (!p.engine)
        throw std::runtime_error("Cannot initialize Filament graphics backend");
    native_surface::release_context(display);
    p.swap = p.engine->createSwapChain(window);
    p.renderer = p.engine->createRenderer();
    p.scene = p.engine->createScene();
    p.view = p.engine->createView();
    p.camera_entity = utils::EntityManager::get().create();
    p.camera = p.engine->createCamera(p.camera_entity);
    if (!p.swap || !p.renderer || !p.scene || !p.view || !p.camera)
        throw std::runtime_error("Cannot allocate rendering resources");
    p.view->setScene(p.scene);
    p.view->setCamera(p.camera);
}
renderer::~renderer() = default;
void renderer::visible_layers(std::uint8_t mask) {
    impl_->view->setVisibleLayers(0xff, mask);
}
void renderer::focus_distance(float value) {
    impl_->camera->setFocusDistance(value);
}
void renderer::configure(const render_options& options) {
    auto& p = *impl_;
    p.renderer->setClearOptions({.clearColor = {options.clear_color.x, options.clear_color.y,
                                                options.clear_color.z, options.clear_color.w},
                                 .clear = true});
    p.camera->setExposure(options.aperture, options.shutter, options.sensitivity);
    p.camera->setFocusDistance(options.focus_distance);
    filament::TemporalAntiAliasingOptions taa;
    taa.enabled = options.temporal_aa;
    p.view->setTemporalAntiAliasingOptions(taa);
    p.view->setAntiAliasing(filament::View::AntiAliasing::FXAA);
    filament::AmbientOcclusionOptions ao;
    ao.enabled = options.occlusion;
    ao.radius = options.occlusion_radius;
    ao.power = options.occlusion_power;
    ao.quality =
        options.occlusion_quality == 2 ? filament::QualityLevel::HIGH : filament::QualityLevel::MEDIUM;
    ao.resolution = options.occlusion_resolution;
    ao.lowPassFilter = filament::QualityLevel::HIGH;
    p.view->setAmbientOcclusionOptions(ao);
    p.view->setDithering(filament::View::Dithering::TEMPORAL);
    p.view->setShadowType(options.soft_shadows ? filament::ShadowType::PCSS : filament::ShadowType::PCF);
    filament::DepthOfFieldOptions dof;
    dof.enabled = options.depth_of_field;
    dof.cocScale = options.blur_scale;
    dof.maxForegroundCOC = options.foreground_blur;
    dof.maxBackgroundCOC = options.background_blur;
    p.view->setDepthOfFieldOptions(dof);
    filament::FogOptions fog;
    fog.enabled = options.fog.enabled;
    fog.distance = options.fog.distance;
    fog.density = options.fog.density;
    fog.cutOffDistance = options.fog.cutoff;
    fog.heightFalloff = options.fog.falloff;
    fog.color = {options.fog.color.x, options.fog.color.y, options.fog.color.z};
    p.view->setFogOptions(fog);
}
filament::Engine& backend_access::engine(renderer& r) {
    return *r.impl_->engine;
}
filament::Scene& backend_access::scene(renderer& r) {
    return *r.impl_->scene;
}
filament::View& backend_access::view(renderer& r) {
    return *r.impl_->view;
}
filament::Camera& backend_access::camera(renderer& r) {
    return *r.impl_->camera;
}
filament::Renderer& backend_access::drawing(renderer& r) {
    return *r.impl_->renderer;
}
bool renderer::frame(const camera_pose& camera, unsigned width, unsigned height, float near_plane,
                     float far_plane, native_hud* overlay, const std::filesystem::path& capture,
                     bool capture_overlay) {
    if (!width || !height)
        return false;
    if (!std::isfinite(near_plane) || !std::isfinite(far_plane) || near_plane <= 0 ||
        far_plane <= near_plane || !std::isfinite(camera.fov) || camera.fov <= 0 || camera.fov >= 180)
        throw std::invalid_argument("Invalid camera projection");
    std::vector<std::uint8_t> pixels;
    if (!capture.empty()) {
        if (std::size_t(width) > std::numeric_limits<std::size_t>::max() / height / 4)
            throw std::length_error("Image dimensions overflow");
        pixels.resize(std::size_t(width) * height * 4);
    }
    auto& p = *impl_;
    p.view->setViewport({0, 0, width, height});
    p.camera->setProjection(camera.fov, double(width) / height, near_plane, far_plane,
                            filament::Camera::Fov::VERTICAL);
    const auto eye = camera.eye, dir = camera.direction;
    p.camera->lookAt({eye.x, eye.y, eye.z}, {eye.x + dir.x, eye.y + dir.y, eye.z + dir.z});
    if (!p.renderer->beginFrame(p.swap))
        return false;
    p.renderer->render(p.view);
    if (overlay && capture_overlay)
        overlay->render(*this);
    bool read_done = false;
    if (!capture.empty())
        p.renderer->readPixels(0, 0, width, height,
                               filament::backend::PixelBufferDescriptor(
                                   pixels.data(), pixels.size(), filament::backend::PixelDataFormat::RGBA,
                                   filament::backend::PixelDataType::UBYTE,
                                   [](void*, size_t, void* context) { *static_cast<bool*>(context) = true; },
                                   &read_done));
    if (overlay && !capture_overlay)
        overlay->render(*this);
    p.renderer->endFrame();
    if (!capture.empty()) {
        p.engine->flushAndWait();
        p.engine->pumpMessageQueues();
        if (!read_done)
            throw std::runtime_error("Screenshot readback did not complete");
        export_image(capture, width, height, pixels);
    }
    return true;
}
}
