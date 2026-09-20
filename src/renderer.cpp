#include "sengine/renderer.hpp"
#include "sengine/image_export.hpp"
#include "sengine/native_hud.hpp"
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
renderer::renderer(void* window, void* shared_context) : impl_(std::make_unique<impl>()) {
    if (!window)
        throw std::invalid_argument("A native window is required");
#ifdef __APPLE__
    constexpr auto backend = filament::Engine::Backend::METAL;
#else
    constexpr auto backend = filament::Engine::Backend::OPENGL;
#endif
    auto& p = *impl_;
    filament::Engine::Config config;
    config.driverHandleArenaSizeMB = 16;
    p.engine = filament::Engine::create(backend, nullptr, shared_context, &config);
    if (!p.engine)
        throw std::runtime_error("Cannot initialize Filament graphics backend");
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
filament::Engine& renderer::engine() {
    return *impl_->engine;
}
filament::Scene& renderer::scene() {
    return *impl_->scene;
}
filament::View& renderer::view() {
    return *impl_->view;
}
filament::Camera& renderer::camera() {
    return *impl_->camera;
}
filament::Renderer& renderer::backend() {
    return *impl_->renderer;
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
        overlay->render(*p.renderer);
    bool read_done = false;
    if (!capture.empty())
        p.renderer->readPixels(0, 0, width, height,
                               filament::backend::PixelBufferDescriptor(
                                   pixels.data(), pixels.size(), filament::backend::PixelDataFormat::RGBA,
                                   filament::backend::PixelDataType::UBYTE,
                                   [](void*, size_t, void* context) { *static_cast<bool*>(context) = true; },
                                   &read_done));
    if (overlay && !capture_overlay)
        overlay->render(*p.renderer);
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
