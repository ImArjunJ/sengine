#include "material_package.hpp"
#include <cstdint>
#include <filament/MaterialChunkType.h>
#include <filament/MaterialEnums.h>
#include <map>
#include <stdexcept>
#include <utils/Hash.h>
#include <vector>

namespace sengine::filament_detail {
namespace {
template <class integer> integer read_integer(std::span<const std::byte> bytes) {
    if (bytes.size() < sizeof(integer))
        throw std::invalid_argument("Truncated material package");
    integer result{};
    for (std::size_t i = 0; i < sizeof(integer); ++i)
        result |= integer(std::to_integer<unsigned>(bytes[i])) << (8 * i);
    return result;
}
class material_package {
  public:
    explicit material_package(std::span<const std::byte> bytes) : bytes_(bytes) {
        auto remaining = bytes;
        while (!remaining.empty()) {
            const auto type = read_integer<std::uint64_t>(remaining);
            remaining = remaining.subspan(8);
            const auto size = read_integer<std::uint32_t>(remaining);
            remaining = remaining.subspan(4);
            if (size > remaining.size() || !chunks_.emplace(type, remaining.first(size)).second)
                throw std::invalid_argument("Invalid material chunk");
            remaining = remaining.subspan(size);
        }
    }
    void validate(filament::backend::Backend backend) const {
        using namespace filamat;
        const auto version = require(MaterialVersion);
        if (version.size() != 4 || read_integer<std::uint32_t>(version) != filament::MATERIAL_VERSION)
            throw std::invalid_argument("Material was compiled for a different engine version");
        const auto checksum = require(MaterialCrc32);
        if (checksum.size() != 4 || checksum.data() + 4 != bytes_.data() + bytes_.size())
            throw std::invalid_argument("Invalid material checksum chunk");
        std::vector<std::uint32_t> table;
        utils::hash::crc32GenerateTable(table);
        const auto actual = utils::hash::crc32Update(0, bytes_.data(), bytes_.size() - 16, table);
        if (actual != read_integer<std::uint32_t>(checksum))
            throw std::invalid_argument("Material checksum mismatch");
        const auto name = require(MaterialName);
        if (name.empty() || name.back() != std::byte{})
            throw std::invalid_argument("Invalid material name");
        bool supported = false;
        switch (backend) {
        case filament::backend::Backend::OPENGL:
            supported = has(MaterialGlsl, DictionaryText) || has(MaterialEssl1, DictionaryText);
            break;
        case filament::backend::Backend::VULKAN:
            supported = has(MaterialSpirv, DictionarySpirv);
            break;
        case filament::backend::Backend::METAL:
            supported =
                has(MaterialMetal, DictionaryText) || has(MaterialMetalLibrary, DictionaryMetalLibrary);
            break;
        default:
            break;
        }
        if (!supported)
            throw std::invalid_argument("Material does not support the active graphics backend");
    }

  private:
    std::span<const std::byte> require(std::uint64_t type) const {
        const auto found = chunks_.find(type);
        if (found == chunks_.end())
            throw std::invalid_argument("Material package is missing a required chunk");
        return found->second;
    }
    bool has(std::uint64_t shader, std::uint64_t dictionary) const {
        const auto code = chunks_.find(shader), strings = chunks_.find(dictionary);
        return code != chunks_.end() && !code->second.empty() && strings != chunks_.end() &&
               !strings->second.empty();
    }

  private:
    std::span<const std::byte> bytes_;
    std::map<std::uint64_t, std::span<const std::byte>> chunks_;
};
}
void validate_material_package(std::span<const std::byte> bytes, filament::backend::Backend backend) {
    material_package(bytes).validate(backend);
}
}
