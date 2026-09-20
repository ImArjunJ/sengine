#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sengine::layout {
template <class measure_function>
std::vector<std::string> wrap_text(std::string_view value, float width, measure_function measure) {
    std::vector<std::string> lines;
    std::string line;
    auto emit = [&] {
        lines.push_back(std::move(line));
        line.clear();
    };
    auto word = [&](std::string_view token) {
        if (token.empty())
            return;
        const auto combined = line.empty() ? std::string(token) : line + ' ' + std::string(token);
        if (measure(combined) <= width) {
            line = combined;
            return;
        }
        if (!line.empty())
            emit();
        for (std::size_t begin = 0; begin < token.size();) {
            std::size_t end = begin + 1;
            while (end < token.size() && (static_cast<unsigned char>(token[end]) & 0xc0) == 0x80)
                ++end;
            const auto glyph = token.substr(begin, end - begin);
            if (!line.empty() && measure(line + std::string(glyph)) > width)
                emit();
            line += glyph;
            begin = end;
        }
    };
    std::size_t begin = 0;
    for (std::size_t i = 0; i < value.size(); ++i)
        if (value[i] == ' ' || value[i] == '\t' || value[i] == '\n') {
            word(value.substr(begin, i - begin));
            if (value[i] == '\n')
                emit();
            begin = i + 1;
        }
    word(value.substr(begin));
    emit();
    return lines;
}
}
