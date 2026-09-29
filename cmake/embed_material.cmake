file(READ "${INPUT}" bytes HEX)
string(REGEX REPLACE "(..)" "0x\\1," bytes "${bytes}")
file(WRITE "${OUTPUT}" "#include \"surface_material.hpp\"\nnamespace sengine::detail {\nstd::span<const std::byte> surface_package() {\n    static constexpr unsigned char bytes[] = {${bytes}};\n    return std::as_bytes(std::span(bytes));\n}\n}\n")
