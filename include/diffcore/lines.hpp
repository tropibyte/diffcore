// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.
//
// Splitting text into lines.

#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace diffcore {

/// A text split into lines. Each line is a view into the text it was split
/// from and excludes its '\n'. A '\r' before the '\n' is part of the line,
/// so "a\r\n" and "a\n" are different lines.
struct Lines {
    std::vector<std::string_view> lines;
    /// The text is not empty and does not end with '\n', so its last line
    /// is incomplete.
    bool missing_final_newline = false;

    std::size_t size() const noexcept { return lines.size(); }
    bool empty() const noexcept { return lines.empty(); }
    std::string_view operator[](std::size_t i) const { return lines[i]; }

    /// True for the last line when it has no '\n'.
    bool is_incomplete(std::size_t i) const noexcept { return missing_final_newline && i + 1 == lines.size(); }
};

/// Splits `text` after each '\n'. The views refer to `text`, which must
/// outlive the result. An empty text has no lines; "a" and "a\n" both have
/// one, and "\n" has one empty line. Any byte other than '\n', NUL
/// included, is line content.
Lines split_lines(std::string_view text);

}  // namespace diffcore
