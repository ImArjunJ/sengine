#include "backends/font_atlas.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <type_traits>

namespace sengine {
namespace {
struct library_deleter {
    void operator()(FT_Library library) const { FT_Done_FreeType(library); }
};
struct face_deleter {
    void operator()(FT_Face face) const { FT_Done_Face(face); }
};
using font_library = std::unique_ptr<std::remove_pointer_t<FT_Library>, library_deleter>;
using font_face = std::unique_ptr<std::remove_pointer_t<FT_Face>, face_deleter>;

font_library open_library() {
    FT_Library library{};
    if (FT_Init_FreeType(&library))
        throw std::runtime_error("Cannot load native HUD font");
    return font_library(library);
}
font_face open_face(FT_Library library, const std::filesystem::path& path, const char* error) {
    FT_Face face{};
    if (FT_New_Face(library, path.c_str(), 0, &face))
        throw std::runtime_error(error);
    font_face owned(face);
    FT_Set_Pixel_Sizes(face, 0, 64);
    return owned;
}

class atlas_builder {
  public:
    atlas_builder() {
        atlas.pixels.assign(font_atlas::width * font_atlas::height * 4, 255);
        for (std::size_t i = 3; i < atlas.pixels.size(); i += 4)
            atlas.pixels[i] = 0;
        for (unsigned y = 0; y < 16; ++y)
            for (unsigned x = 0; x < 16; ++x)
                atlas.pixels[(y * font_atlas::width + x) * 4 + 3] = 255;
    }

    font_atlas build(const std::filesystem::path& font, const std::filesystem::path& display_font) {
        auto library = open_library();
        add_font(open_face(library.get(), font, "Cannot load native HUD font").get(), 0);
        new_row();
        add_font(
            open_face(library.get(), display_font.empty() ? font : display_font, "Display font unavailable")
                .get(),
            1);
        atlas.used_height = y + row + padding;
        return std::move(atlas);
    }

  private:
    static constexpr int padding = 4;
    font_atlas atlas;
    int x{24}, y{padding}, row{16};

  private:
    void new_row() {
        x = padding;
        y += row + padding * 2;
        row = 0;
    }
    void add_font(FT_Face face, int index) {
        constexpr std::array<unsigned, 4> punctuation{0x2013, 0x2014, 0x2019, 0x2026};
        for (int c = 32; c < 260; ++c) {
            const unsigned codepoint = c < 256 ? unsigned(c) : punctuation[c - 256];
            if (!FT_Load_Char(face, codepoint, FT_LOAD_RENDER))
                add_glyph(*face->glyph, c + index * 384);
        }
    }
    void add_glyph(const FT_GlyphSlotRec& glyph, int index) {
        if (x + glyph.bitmap.width + padding > font_atlas::width)
            new_row();
        if (y + glyph.bitmap.rows >= 1980)
            throw std::runtime_error("Font atlas capacity exceeded");
        for (unsigned by = 0; by < glyph.bitmap.rows; ++by)
            for (unsigned bx = 0; bx < glyph.bitmap.width; ++bx)
                atlas.pixels[((y + by) * font_atlas::width + x + bx) * 4 + 3] =
                    glyph.bitmap.buffer[by * glyph.bitmap.pitch + bx];
        atlas.glyphs[index] = {float(x) / 1024,
                               float(y) / 2048,
                               float(glyph.bitmap.width),
                               float(glyph.bitmap.rows),
                               float(glyph.bitmap_left),
                               float(glyph.bitmap_top),
                               float(glyph.advance.x) / 64};
        x += glyph.bitmap.width + padding * 2;
        row = std::max(row, int(glyph.bitmap.rows));
    }
};
}
font_atlas rasterize_fonts(const std::filesystem::path& body_font,
                           const std::filesystem::path& display_font) {
    return atlas_builder{}.build(body_font, display_font);
}
}
