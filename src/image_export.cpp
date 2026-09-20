#include "sengine/image_export.hpp"
#include <fstream>
#include <limits>
#include <png.h>
#include <stdexcept>

namespace sengine {
void export_image(const std::filesystem::path& path, unsigned width, unsigned height,
                  std::span<const std::uint8_t> rgba) {
    if (!width || !height || width > std::numeric_limits<std::size_t>::max() / 4 / height ||
        rgba.size() != std::size_t(width) * height * 4)
        throw std::invalid_argument("Invalid photograph dimensions");
    auto temporary = path;
    temporary += ".tmp";
    try {
        if (path.extension() == ".png") {
            png_image image{};
            image.version = PNG_IMAGE_VERSION;
            image.width = width;
            image.height = height;
            image.format = PNG_FORMAT_RGBA;
            bool written = png_image_write_to_file(&image, temporary.c_str(), 0, rgba.data(), 0, nullptr);
            std::string error = image.message;
            png_image_free(&image);
            if (!written)
                throw std::runtime_error("Cannot save photograph: " + error);
        } else {
            std::ofstream output(temporary, std::ios::binary);
            output << "P6\n" << width << ' ' << height << "\n255\n";
            for (std::size_t i = 0; i < rgba.size(); i += 4)
                output.write(reinterpret_cast<const char*>(rgba.data() + i), 3);
            output.close();
            if (!output)
                throw std::runtime_error("Cannot save photograph");
        }
        std::filesystem::rename(temporary, path);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}
}
