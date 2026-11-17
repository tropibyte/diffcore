// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.

#include "diffcore/lines.hpp"

#include <cstddef>
#include <string_view>

namespace diffcore {

Lines split_lines(std::string_view text) {
    Lines out;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t newline = text.find('\n', start);
        if (newline == std::string_view::npos) {
            out.lines.push_back(text.substr(start));
            out.missing_final_newline = true;
            break;
        }
        out.lines.push_back(text.substr(start, newline - start));
        start = newline + 1;
    }
    return out;
}

}  // namespace diffcore
