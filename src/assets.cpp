#include "sengine/assets.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>
namespace sengine {
asset_store::asset_store(std::filesystem::path root) {
    mount("", std::move(root));
}
void asset_store::require_owner() const {
    if (std::this_thread::get_id() != owner_)
        throw std::logic_error("Asset storage belongs to its creating thread");
}
void asset_store::mount(std::string name, std::filesystem::path directory) {
    require_owner();
    if (name.find_first_of(":/\\") != std::string::npos)
        throw std::invalid_argument("Invalid asset mount name");
    directory = std::filesystem::canonical(directory);
    if (!std::filesystem::is_directory(directory))
        throw std::invalid_argument("Asset mount must be a directory");
    mounts_.insert_or_assign(std::move(name), std::move(directory));
}
std::filesystem::path asset_store::resolve(const std::string& uri) const {
    require_owner();
    const auto separator = uri.find(':');
    const auto mount = separator == std::string::npos ? std::string{} : uri.substr(0, separator);
    const auto relative =
        std::filesystem::path(separator == std::string::npos ? uri : uri.substr(separator + 1));
    if (relative.empty() || relative.is_absolute() || relative.has_root_name())
        throw std::invalid_argument("Assets need a relative URI");
    const auto found = mounts_.find(mount);
    if (found == mounts_.end())
        throw std::invalid_argument("Unknown asset mount: " + mount);
    const auto resolved = std::filesystem::weakly_canonical(found->second / relative);
    auto base = found->second.begin(), end = found->second.end(), candidate = resolved.begin();
    for (; base != end; ++base, ++candidate)
        if (candidate == resolved.end() || *base != *candidate)
            throw std::invalid_argument("Asset path escapes its mount");
    return resolved;
}
std::size_t asset_store::collect_unused() {
    require_owner();
    return std::erase_if(slots_, [](const auto& entry) { return entry.second.use_count() == 1; });
}
std::vector<std::byte> read_binary(const std::filesystem::path& path, std::size_t maximum_bytes) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
        throw std::runtime_error("Cannot open asset: " + path.string());
    const auto size = input.tellg();
    if (size < 0 || std::uintmax_t(size) > maximum_bytes)
        throw std::length_error("Asset exceeds size limit: " + path.string());
    std::vector<std::byte> result(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!result.empty() &&
        !input.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(result.size())))
        throw std::runtime_error("Cannot read asset: " + path.string());
    return result;
}
std::string read_text(const std::filesystem::path& path, std::size_t maximum_bytes) {
    const auto data = read_binary(path, maximum_bytes);
    if (data.empty())
        return {};
    return {reinterpret_cast<const char*>(data.data()), data.size()};
}
}
