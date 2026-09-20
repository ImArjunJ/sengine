#pragma once
#include <cstdint>
#include <filesystem>
#include <span>

namespace sengine {
void export_image(const std::filesystem::path&, unsigned width, unsigned height,
                  std::span<const std::uint8_t> rgba);
}
