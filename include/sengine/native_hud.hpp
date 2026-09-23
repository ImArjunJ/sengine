#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace sengine {
class renderer;
struct ink {
    float r, g, b, a{1};
};
struct hud_vertex {
    float x, y, z, u, v, r, g, b, a;
};
struct hud_image {
    std::size_t value{};

  public:
    explicit operator bool() const { return value != 0; }
};
struct hud_input {
    float x{}, y{}, wheel{};
    bool pressed{}, released{}, down{};
};
class native_hud {
  public:
    native_hud(renderer&, const std::filesystem::path& material, const std::filesystem::path& font,
               const std::filesystem::path& display_font = {});
    ~native_hud();
    void begin(unsigned width, unsigned height, float scale);
    void rectangle(float x, float y, float w, float h, ink);
    void line(float x, float y, float x2, float y2, float thickness, ink);
    void text(float x, float y, const std::string&, float size, ink, int face = 0);
    float measure(const std::string&, float size, int face = 0) const;
    void triangle(std::array<float, 2>, std::array<float, 2>, std::array<float, 2>, ink, ink, ink);
    void clip(float x, float y, float width, float height);
    void clear_clip();
    hud_image upload_image(unsigned width, unsigned height, std::span<const std::uint8_t> rgba);
    void image(hud_image, float x, float y, float width, float height, ink tint = {1, 1, 1, 1});
    void tiled_image(hud_image, float x, float y, float width, float height, float tile_width,
                     float tile_height, ink tint = {1, 1, 1, 1});
    void render(renderer&);
    float width() const { return width_ / scale_; }
    float height() const { return height_ / scale_; }

  private:
    struct impl;
    std::unique_ptr<impl> impl_;
    std::vector<hud_vertex> vertices_;
    unsigned width_{}, height_{};
    float scale_{1};
    std::array<float, 4> clip_{};

  private:
    void emit(hud_vertex a, hud_vertex b, hud_vertex c);
    void quad(float x, float y, float w, float h, float u, float v, float uw, float vh, ink c);
};
}
