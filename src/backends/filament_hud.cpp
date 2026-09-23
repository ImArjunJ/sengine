#include "backends/buffer_storage.hpp"
#include "backends/filament_access.hpp"
#include "backends/font_atlas.hpp"
#include "backends/hud_geometry.hpp"
#include "sengine/native_hud.hpp"
#include <algorithm>
#include <array>
#include <backend/PixelBufferDescriptor.h>
#include <cmath>
#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <fstream>
#include <numeric>
#include <stdexcept>
#include <utils/EntityManager.h>
using namespace filament;
namespace sengine {
namespace {
unsigned glyph_index(unsigned cp) {
    if (cp < 256)
        return cp;
    switch (cp) {
    case 0x2013:
        return 256;
    case 0x2014:
        return 257;
    case 0x2019:
        return 258;
    case 0x2026:
        return 259;
    default:
        return '?';
    }
}
std::vector<unsigned> glyphs(std::string_view text) {
    std::vector<unsigned> out;
    for (size_t i = 0; i < text.size();) {
        unsigned char c = text[i++];
        unsigned cp = c;
        int extra = 0;
        if (c >= 0xc2 && c <= 0xdf) {
            cp = c & 31;
            extra = 1;
        } else if (c >= 0xe0 && c <= 0xef) {
            cp = c & 15;
            extra = 2;
        } else if (c >= 0xf0 && c <= 0xf4) {
            cp = c & 7;
            extra = 3;
        } else if (c >= 128)
            cp = '?';
        while (extra--) {
            if (i >= text.size() || (static_cast<unsigned char>(text[i]) & 0xc0) != 0x80) {
                cp = '?';
                break;
            }
            cp = (cp << 6) | (static_cast<unsigned char>(text[i++]) & 63);
        }
        out.push_back(glyph_index(cp));
    }
    return out;
}
}

struct native_hud::impl {
    Engine& e;
    Scene* scene{};
    View* view{};
    Camera* camera{};
    Material* material{};
    MaterialInstance* instance{};
    Texture* atlas{};
    VertexBuffer* vb{};
    IndexBuffer* ib{};
    utils::Entity entity{}, camera_entity{};
    std::array<atlas_glyph, 768> glyph{};
    struct image_region {
        unsigned x, y, width, height;
    };
    std::vector<image_region> images;
    unsigned image_x{font_atlas::width}, image_y{font_atlas::height}, image_row{}, font_height{};

  public:
    explicit impl(Engine& engine) : e(engine) {}
    void load_material(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        std::vector<char> data((std::istreambuf_iterator<char>(in)), {});
        if (data.empty())
            throw std::runtime_error("HUD material unavailable");
        material = Material::Builder().package(data.data(), data.size()).build(e);
        if (!material)
            throw std::runtime_error("Invalid HUD material");
        instance = material->createInstance();
        if (!instance)
            throw std::runtime_error("Cannot create HUD material instance");
    }
    void load_font(const std::filesystem::path& font, const std::filesystem::path& display_font) {
        auto rasterized = rasterize_fonts(font, display_font);
        glyph = rasterized.glyphs;
        font_height = rasterized.used_height;
        auto pixels = std::make_unique<std::vector<std::uint8_t>>(std::move(rasterized.pixels));
        atlas = Texture::Builder()
                    .width(1024)
                    .height(2048)
                    .levels(3)
                    .usage(Texture::Usage::SAMPLEABLE | Texture::Usage::UPLOADABLE |
                           Texture::Usage::GEN_MIPMAPPABLE)
                    .format(Texture::InternalFormat::RGBA8)
                    .build(e);
        auto* uploaded_pixels = pixels.get();
        backend::PixelBufferDescriptor image(uploaded_pixels->data(), uploaded_pixels->size(),
                                             backend::PixelDataFormat::RGBA, backend::PixelDataType::UBYTE,
                                             filament_detail::release_vector<uint8_t>, uploaded_pixels);
        pixels.release();
        atlas->setImage(e, 0, std::move(image));
        atlas->generateMipmaps(e);
        instance->setParameter("atlas", atlas,
                               TextureSampler(TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
                                              TextureSampler::MagFilter::LINEAR));
    }
    void create_buffers() {
        vb = VertexBuffer::Builder()
                 .vertexCount(120000)
                 .bufferCount(1)
                 .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3, 0,
                            sizeof(hud_vertex))
                 .attribute(VertexAttribute::UV0, 0, VertexBuffer::AttributeType::FLOAT2, 12,
                            sizeof(hud_vertex))
                 .attribute(VertexAttribute::COLOR, 0, VertexBuffer::AttributeType::FLOAT4, 20,
                            sizeof(hud_vertex))
                 .build(e);
        ib = IndexBuffer::Builder().indexCount(120000).bufferType(IndexBuffer::IndexType::UINT).build(e);
        auto* indices = new std::vector<uint32_t>(120000);
        std::iota(indices->begin(), indices->end(), 0);
        ib->setBuffer(e, IndexBuffer::BufferDescriptor(indices->data(), indices->size() * 4,
                                                       filament_detail::release_vector<uint32_t>, indices));
    }
    void create_geometry() {
        entity = utils::EntityManager::get().create();
        e.getTransformManager().create(entity);
        RenderableManager::Builder(1)
            .boundingBox({{0, 0, 0}, {2, 2, 2}})
            .material(0, instance)
            .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, vb, ib, 0, 0)
            .culling(false)
            .castShadows(false)
            .receiveShadows(false)
            .build(e, entity);
    }
    void create_view() {
        scene = e.createScene();
        scene->addEntity(entity);
        camera_entity = utils::EntityManager::get().create();
        camera = e.createCamera(camera_entity);
        camera->setProjection(Camera::Projection::ORTHO, -1., 1., -1., 1., .1, 10.);
        camera->lookAt({0, 0, 1}, {0, 0, 0});
        view = e.createView();
        view->setScene(scene);
        view->setCamera(camera);
        view->setPostProcessingEnabled(false);
        view->setBlendMode(View::BlendMode::TRANSLUCENT);
        view->setShadowingEnabled(false);
    }
    ~impl() {
        e.destroy(view);
        e.destroy(scene);
        e.destroy(entity);
        e.destroyCameraComponent(camera_entity);
        utils::EntityManager::get().destroy(entity);
        utils::EntityManager::get().destroy(camera_entity);
        e.destroy(vb);
        e.destroy(ib);
        e.destroy(instance);
        e.destroy(material);
        e.destroy(atlas);
    }
};
native_hud::native_hud(renderer& graphics, const std::filesystem::path& material,
                       const std::filesystem::path& font, const std::filesystem::path& display_font)
    : impl_(std::make_unique<impl>(backend_access::engine(graphics))) {
    impl_->load_material(material);
    impl_->load_font(font, display_font);
    impl_->create_buffers();
    impl_->create_geometry();
    impl_->create_view();
}
native_hud::~native_hud() = default;
void native_hud::begin(unsigned w, unsigned h, float scale) {
    width_ = w;
    height_ = h;
    scale_ = std::clamp(scale, .65f, 3.f);
    vertices_.clear();
    clear_clip();
}
void native_hud::clip(float x, float y, float width, float height) {
    clip_ = {x, y, x + width, y + height};
}
void native_hud::clear_clip() {
    clip_ = {0, 0, width(), height()};
}
void native_hud::emit(hud_vertex a, hud_vertex b, hud_vertex c) {
    hud_clipper(vertices_, clip_, width_, height_, scale_).emit(a, b, c);
}
void native_hud::triangle(std::array<float, 2> a, std::array<float, 2> b, std::array<float, 2> c, ink ca,
                          ink cb, ink cc) {
    emit(solid_vertex(a, ca), solid_vertex(b, cb), solid_vertex(c, cc));
}
void native_hud::quad(float x, float y, float w, float h, float u, float v, float uw, float vh, ink color) {
    const hud_vertex top_left{x, y, 0, u, v, color.r, color.g, color.b, color.a};
    const hud_vertex top_right{x + w, y, 0, u + uw, v, color.r, color.g, color.b, color.a};
    const hud_vertex bottom_right{x + w, y + h, 0, u + uw, v + vh, color.r, color.g, color.b, color.a};
    const hud_vertex bottom_left{x, y + h, 0, u, v + vh, color.r, color.g, color.b, color.a};
    emit(top_left, top_right, bottom_right);
    emit(top_left, bottom_right, bottom_left);
}
void native_hud::rectangle(float x, float y, float w, float h, ink c) {
    quad(x, y, w, h, 8.f / 1024, 8.f / 2048, 0, 0, c);
}
hud_image native_hud::upload_image(unsigned width, unsigned height, std::span<const std::uint8_t> rgba) {
    if (!width || !height || width > font_atlas::width || height > font_atlas::height ||
        rgba.size() != std::size_t(width) * height * 4)
        throw std::invalid_argument("Invalid HUD image dimensions");
    auto& state = *impl_;
    auto x = state.image_x, y = state.image_y, row = state.image_row;
    if (width > x) {
        x = font_atlas::width;
        y -= row;
        row = 0;
    }
    row = std::max(row, height);
    if (row > y || y - row < state.font_height)
        throw std::length_error("HUD image atlas is full");
    x -= width;
    auto pixels = std::make_unique<std::vector<std::uint8_t>>(rgba.begin(), rgba.end());
    state.images.reserve(state.images.size() + 1);
    auto* transferred = pixels.get();
    backend::PixelBufferDescriptor image(transferred->data(), transferred->size(),
                                         backend::PixelDataFormat::RGBA, backend::PixelDataType::UBYTE,
                                         filament_detail::release_vector<std::uint8_t>, transferred);
    pixels.release();
    state.atlas->setImage(state.e, 0, x, y - height, width, height, std::move(image));
    state.atlas->generateMipmaps(state.e);
    state.images.push_back({x, y - height, width, height});
    state.image_x = x;
    state.image_y = y;
    state.image_row = row;
    return {state.images.size()};
}
void native_hud::image(hud_image id, float x, float y, float width, float height, ink tint) {
    const auto& region = impl_->images.at(id.value - 1);
    quad(x, y, width, height, (region.x + .5f) / font_atlas::width, (region.y + .5f) / font_atlas::height,
         float(region.width - 1) / font_atlas::width, float(region.height - 1) / font_atlas::height, tint);
}
void native_hud::tiled_image(hud_image id, float x, float y, float width, float height, float tile_width,
                             float tile_height, ink tint) {
    if (!std::isfinite(tile_width) || !std::isfinite(tile_height) || tile_width <= 0 || tile_height <= 0 ||
        !std::isfinite(width) || !std::isfinite(height) || width < 0 || height < 0)
        throw std::invalid_argument("Invalid HUD tile dimensions");
    const auto& region = impl_->images.at(id.value - 1);
    for (float yy = 0; yy < height; yy += tile_height)
        for (float xx = 0; xx < width; xx += tile_width) {
            const float w = std::min(tile_width, width - xx), h = std::min(tile_height, height - yy);
            quad(x + xx, y + yy, w, h, (region.x + .5f) / font_atlas::width,
                 (region.y + .5f) / font_atlas::height,
                 w / tile_width * (region.width - 1) / font_atlas::width,
                 h / tile_height * (region.height - 1) / font_atlas::height, tint);
        }
}
void native_hud::line(float x, float y, float x2, float y2, float thickness, ink c) {
    float dx = x2 - x, dy = y2 - y, len = std::hypot(dx, dy);
    if (len < .001f)
        return;
    float nx = -dy / len * thickness * .5f, ny = dx / len * thickness * .5f;
    std::array<float, 2> a{x + nx, y + ny}, b{x2 + nx, y2 + ny}, cc{x2 - nx, y2 - ny}, d{x - nx, y - ny};
    triangle(a, b, cc, c, c, c);
    triangle(a, cc, d, c, c, c);
}
float native_hud::measure(const std::string& text, float size, int face) const {
    float current = 0, largest = 0;
    for (unsigned ch : glyphs(text)) {
        if (ch == '\n') {
            largest = std::max(largest, current);
            current = 0;
        } else
            current += impl_->glyph[ch + (face ? 384 : 0)].advance * size / 64;
    }
    return std::max(largest, current);
}
void native_hud::text(float x, float y, const std::string& s, float size, ink c, int face) {
    float origin = x;
    for (unsigned ch : glyphs(s)) {
        if (ch == '\n') {
            y += size * 1.35f;
            x = origin;
            continue;
        }
        const auto& g = impl_->glyph[ch + (face ? 384 : 0)];
        float t = size / 64;
        quad(x + g.left * t, y + (50 - g.top) * t, g.w * t, g.h * t, g.u, g.v, g.w / 1024, g.h / 2048, c);
        x += g.advance * t;
    }
}
void native_hud::render(renderer& graphics) {
    auto& r = backend_access::drawing(graphics);
    if (vertices_.empty())
        return;
    if (vertices_.size() > 120000)
        throw std::runtime_error("HUD geometry budget exceeded");
    auto& p = *impl_;
    auto* copy = new std::vector<hud_vertex>(vertices_);
    p.vb->setBufferAt(p.e, 0,
                      VertexBuffer::BufferDescriptor(copy->data(), copy->size() * sizeof(hud_vertex),
                                                     filament_detail::release_vector<hud_vertex>, copy));
    p.e.getRenderableManager().setGeometryAt(p.e.getRenderableManager().getInstance(p.entity), 0,
                                             RenderableManager::PrimitiveType::TRIANGLES, p.vb, p.ib, 0,
                                             vertices_.size());
    p.view->setViewport({0, 0, width_, height_});
    r.render(p.view);
}
}
