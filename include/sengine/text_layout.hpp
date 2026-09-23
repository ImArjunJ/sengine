#pragma once
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sengine::layout {
namespace detail {
template <class measure_function> class text_wrapper {
  public:
    text_wrapper(float width, measure_function measure) : width_(width), measure_(std::move(measure)) {}
    std::vector<std::string> wrap(std::string_view value) {
        std::size_t begin = 0;
        for (std::size_t i = 0; i < value.size(); ++i)
            if (value[i] == ' ' || value[i] == '\t' || value[i] == '\n') {
                append_word(value.substr(begin, i - begin));
                if (value[i] == '\n')
                    emit_line();
                begin = i + 1;
            }
        append_word(value.substr(begin));
        emit_line();
        return std::move(lines_);
    }

  private:
    void emit_line() {
        lines_.push_back(std::move(line_));
        line_.clear();
    }
    void append_word(std::string_view token) {
        if (token.empty())
            return;
        const auto combined = line_.empty() ? std::string(token) : line_ + ' ' + std::string(token);
        if (std::invoke(measure_, combined) <= width_) {
            line_ = combined;
            return;
        }
        if (!line_.empty())
            emit_line();
        append_glyphs(token);
    }
    void append_glyphs(std::string_view token) {
        for (std::size_t begin = 0; begin < token.size();) {
            std::size_t end = begin + 1;
            while (end < token.size() && (static_cast<unsigned char>(token[end]) & 0xc0) == 0x80)
                ++end;
            const auto glyph = token.substr(begin, end - begin);
            if (!line_.empty() && std::invoke(measure_, line_ + std::string(glyph)) > width_)
                emit_line();
            line_ += glyph;
            begin = end;
        }
    }

  private:
    float width_;
    measure_function measure_;
    std::vector<std::string> lines_;
    std::string line_;
};
}
template <class measure_function>
std::vector<std::string> wrap_text(std::string_view value, float width, measure_function measure) {
    return detail::text_wrapper(width, std::move(measure)).wrap(value);
}
}
