#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace sengine {
struct atlas_glyph {
    float u, v, w, h, left, top, advance;
};
struct font_atlas {
    static constexpr unsigned width = 1024, height = 2048;
    std::array<atlas_glyph, 768> glyphs{};
    std::vector<std::uint8_t> pixels;
    unsigned used_height{};
};
font_atlas rasterize_fonts(const std::filesystem::path& body_font, const std::filesystem::path& display_font);
}
