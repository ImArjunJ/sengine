#include "sengine/native_hud.hpp"
#include <algorithm>
#include <backend/PixelBufferDescriptor.h>
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
#include <ft2build.h>
#include <utils/EntityManager.h>
#include FT_FREETYPE_H
#include <array>
#include <cmath>
#include <fstream>
#include <numeric>
#include <stdexcept>
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
    struct glyph {
        float u, v, w, h, left, top, advance;
    };
    std::array<glyph, 768> glyph{};
    explicit impl(Engine& engine) : e(engine) {}
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
native_hud::native_hud(Engine& e, const std::filesystem::path& material, const std::filesystem::path& font)
    : impl_(std::make_unique<impl>(e)) {
    auto& p = *impl_;
    std::ifstream in(material, std::ios::binary);
    std::vector<char> data((std::istreambuf_iterator<char>(in)), {});
    if (data.empty())
        throw std::runtime_error("HUD material unavailable");
    p.material = Material::Builder().package(data.data(), data.size()).build(e);
    p.instance = p.material->createInstance();
    FT_Library ft{};
    FT_Face face{};
    if (FT_Init_FreeType(&ft) || FT_New_Face(ft, font.c_str(), 0, &face))
        throw std::runtime_error("Cannot load native HUD font");
    FT_Set_Pixel_Sizes(face, 0, 64);
    auto* pixels = new std::vector<uint8_t>(1024 * 2048 * 4, 255);
    for (size_t i = 3; i < pixels->size(); i += 4)
        (*pixels)[i] = 0;
    for (unsigned y = 0; y < 16; ++y)
        for (unsigned x = 0; x < 16; ++x)
            (*pixels)[(y * 1024 + x) * 4 + 3] = 255;
    constexpr int padding = 4;
    int x = 24, y = padding, row = 16;
    for (int font_index = 0; font_index < 2; ++font_index) {
        if (font_index) {
            FT_Done_Face(face);
            if (FT_New_Face(ft, (font.parent_path() / "Display.ttf").c_str(), 0, &face))
                throw std::runtime_error("Display font unavailable");
            FT_Set_Pixel_Sizes(face, 0, 64);
            x = padding;
            y += row + padding * 2;
            row = 0;
        }
        for (int c = 32; c < 260; ++c) {
            unsigned cp = c;
            const unsigned punctuation[] = {0x2013, 0x2014, 0x2019, 0x2026};
            if (c >= 256)
                cp = punctuation[c - 256];
            if (FT_Load_Char(face, cp, FT_LOAD_RENDER))
                continue;
            auto* g = face->glyph;
            if (x + g->bitmap.width + padding > 1024) {
                x = padding;
                y += row + padding * 2;
                row = 0;
            }
            if (y + g->bitmap.rows >= 1980)
                throw std::runtime_error("Font atlas capacity exceeded");
            for (unsigned by = 0; by < g->bitmap.rows; ++by)
                for (unsigned bx = 0; bx < g->bitmap.width; ++bx)
                    (*pixels)[((y + by) * 1024 + x + bx) * 4 + 3] =
                        g->bitmap.buffer[by * g->bitmap.pitch + bx];
            p.glyph[c + font_index * 384] = {
                float(x) / 1024,       float(y) / 2048,      float(g->bitmap.width),  float(g->bitmap.rows),
                float(g->bitmap_left), float(g->bitmap_top), float(g->advance.x) / 64};
            x += g->bitmap.width + padding * 2;
            row = std::max(row, int(g->bitmap.rows));
        }
    }

    std::uint32_t noise = 723;
    for (unsigned py = 1984; py < 2048; ++py)
        for (unsigned px = 960; px < 1024; ++px) {
            noise = noise * 1664525u + 1013904223u;
            int delta = (int(noise >> 28) - 8) / 2;
            auto offset = (py * 1024 + px) * 4;
            (*pixels)[offset] = 235 + delta;
            (*pixels)[offset + 1] = 224 + delta;
            (*pixels)[offset + 2] = 198 + delta;
            (*pixels)[offset + 3] = 255;
        }
    FT_Done_Face(face);
    FT_Done_FreeType(ft);
    p.atlas =
        Texture::Builder()
            .width(1024)
            .height(2048)
            .levels(3)
            .usage(Texture::Usage::SAMPLEABLE | Texture::Usage::UPLOADABLE | Texture::Usage::GEN_MIPMAPPABLE)
            .format(Texture::InternalFormat::RGBA8)
            .build(e);
    p.atlas->setImage(
        e, 0,
        backend::PixelBufferDescriptor(
            pixels->data(), pixels->size(), backend::PixelDataFormat::RGBA, backend::PixelDataType::UBYTE,
            [](void*, size_t, void* u) { delete static_cast<std::vector<uint8_t>*>(u); }, pixels));
    p.atlas->generateMipmaps(e);
    p.instance->setParameter(
        "atlas", p.atlas,
        TextureSampler(TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR, TextureSampler::MagFilter::LINEAR));
    p.vb =
        VertexBuffer::Builder()
            .vertexCount(120000)
            .bufferCount(1)
            .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3, 0,
                       sizeof(hud_vertex))
            .attribute(VertexAttribute::UV0, 0, VertexBuffer::AttributeType::FLOAT2, 12, sizeof(hud_vertex))
            .attribute(VertexAttribute::COLOR, 0, VertexBuffer::AttributeType::FLOAT4, 20, sizeof(hud_vertex))
            .build(e);
    p.ib = IndexBuffer::Builder().indexCount(120000).bufferType(IndexBuffer::IndexType::UINT).build(e);
    auto* indices = new std::vector<uint32_t>(120000);
    std::iota(indices->begin(), indices->end(), 0);
    p.ib->setBuffer(e, IndexBuffer::BufferDescriptor(
                           indices->data(), indices->size() * 4,
                           [](void*, size_t, void* u) { delete static_cast<std::vector<uint32_t>*>(u); },
                           indices));
    p.entity = utils::EntityManager::get().create();
    e.getTransformManager().create(p.entity);
    RenderableManager::Builder(1)
        .boundingBox({{0, 0, 0}, {2, 2, 2}})
        .material(0, p.instance)
        .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, p.vb, p.ib, 0, 0)
        .culling(false)
        .castShadows(false)
        .receiveShadows(false)
        .build(e, p.entity);
    p.scene = e.createScene();
    p.scene->addEntity(p.entity);
    p.camera_entity = utils::EntityManager::get().create();
    p.camera = e.createCamera(p.camera_entity);
    p.camera->setProjection(Camera::Projection::ORTHO, -1., 1., -1., 1., .1, 10.);
    p.camera->lookAt({0, 0, 1}, {0, 0, 0});
    p.view = e.createView();
    p.view->setScene(p.scene);
    p.view->setCamera(p.camera);
    p.view->setPostProcessingEnabled(false);
    p.view->setBlendMode(View::BlendMode::TRANSLUCENT);
    p.view->setShadowingEnabled(false);
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
    auto append = [&](hud_vertex v) {
        v.x = 2 * v.x * scale_ / width_ - 1;
        v.y = 1 - 2 * v.y * scale_ / height_;
        vertices_.push_back(v);
    };
    auto inside = [&](hud_vertex v) {
        return v.x >= clip_[0] && v.y >= clip_[1] && v.x <= clip_[2] && v.y <= clip_[3];
    };
    if (inside(a) && inside(b) && inside(c)) {
        append(a);
        append(b);
        append(c);
        return;
    }
    if (std::max({a.x, b.x, c.x}) < clip_[0] || std::min({a.x, b.x, c.x}) > clip_[2] ||
        std::max({a.y, b.y, c.y}) < clip_[1] || std::min({a.y, b.y, c.y}) > clip_[3])
        return;
    std::array<hud_vertex, 12> polygon{}, next{};
    polygon[0] = a;
    polygon[1] = b;
    polygon[2] = c;
    size_t count = 3;
    auto coordinate = [](const hud_vertex& v, unsigned axis) { return axis == 0 ? v.x : v.y; };
    auto mix = [](hud_vertex a, hud_vertex b, float t) {
        return hud_vertex{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, 0,
                          a.u + (b.u - a.u) * t, a.v + (b.v - a.v) * t, a.r + (b.r - a.r) * t,
                          a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
    };
    for (unsigned edge = 0; edge < 4 && count; ++edge) {
        unsigned axis = edge % 2;
        float limit = clip_[edge];
        auto inside_edge = [&](hud_vertex v) {
            return edge < 2 ? coordinate(v, axis) >= limit : coordinate(v, axis) <= limit;
        };
        size_t next_count = 0;
        auto previous = polygon[count - 1];
        bool before = inside_edge(previous);
        for (size_t i = 0; i < count; ++i) {
            auto current = polygon[i];
            bool after = inside_edge(current);
            if (before != after)
                next[next_count++] = mix(previous, current,
                                         (limit - coordinate(previous, axis)) /
                                             (coordinate(current, axis) - coordinate(previous, axis)));
            if (after)
                next[next_count++] = current;
            previous = current;
            before = after;
        }
        polygon = next;
        count = next_count;
    }
    for (size_t i = 1; i + 1 < count; ++i) {
        append(polygon[0]);
        append(polygon[i]);
        append(polygon[i + 1]);
    }
}
void native_hud::triangle(std::array<float, 2> a, std::array<float, 2> b, std::array<float, 2> c, ink ca,
                          ink cb, ink cc) {
    auto vertex = [](auto p, ink v) {
        return hud_vertex{p[0], p[1], 0, 8.f / 1024, 8.f / 2048, v.r, v.g, v.b, v.a};
    };
    emit(vertex(a, ca), vertex(b, cb), vertex(c, cc));
}
void native_hud::quad(float x, float y, float w, float h, float u, float v, float uw, float vh, ink c) {
    auto vertex = [&](float dx, float dy) {
        return hud_vertex{x + w * dx, y + h * dy, 0, u + uw * dx, v + vh * dy, c.r, c.g, c.b, c.a};
    };
    emit(vertex(0, 0), vertex(1, 0), vertex(1, 1));
    emit(vertex(0, 0), vertex(1, 1), vertex(0, 1));
}
void native_hud::rectangle(float x, float y, float w, float h, ink c) {
    quad(x, y, w, h, 8.f / 1024, 8.f / 2048, 0, 0, c);
}
void native_hud::paper_texture(float x, float y, float w, float h) {
    for (float yy = 0; yy < h; yy += 128)
        for (float xx = 0; xx < w; xx += 128) {
            float width = std::min(128.f, w - xx), height = std::min(128.f, h - yy);
            quad(x + xx, y + yy, width, height, 960.5f / 1024, 1984.5f / 2048, width / 128 * 63 / 1024,
                 height / 128 * 63 / 2048, {1, 1, 1, 1});
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
    if (face < 0)
        face = size >= 26 ? 1 : 0;
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
void native_hud::render(Renderer& r) {
    if (vertices_.empty())
        return;
    if (vertices_.size() > 120000)
        throw std::runtime_error("HUD geometry budget exceeded");
    auto& p = *impl_;
    auto* copy = new std::vector<hud_vertex>(vertices_);
    p.vb->setBufferAt(p.e, 0,
                      VertexBuffer::BufferDescriptor(
                          copy->data(), copy->size() * sizeof(hud_vertex),
                          [](void*, size_t, void* u) { delete static_cast<std::vector<hud_vertex>*>(u); },
                          copy));
    p.e.getRenderableManager().setGeometryAt(p.e.getRenderableManager().getInstance(p.entity), 0,
                                             RenderableManager::PrimitiveType::TRIANGLES, p.vb, p.ib, 0,
                                             vertices_.size());
    p.view->setViewport({0, 0, width_, height_});
    r.render(p.view);
}
}
