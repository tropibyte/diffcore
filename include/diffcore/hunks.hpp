// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.
//
// Grouping an edit script into hunks with context.

#pragma once

#include <cstddef>
#include <vector>

#include "diffcore/diff.hpp"

namespace diffcore {

/// A group of nearby changes with the unchanged lines around them.
struct Hunk {
    /// First old line (0-based). When old_count is 0, the old line the
    /// hunk's new lines go before.
    std::size_t old_start = 0;
    std::size_t old_count = 0;
    /// First new line (0-based); likewise when new_count is 0.
    std::size_t new_start = 0;
    std::size_t new_count = 0;
    /// The part of the script the hunk covers: leading context, changes and
    /// the unchanged lines between them, trailing context.
    EditScript edits;
};

/// Groups the changes of `script` into hunks with up to `context` unchanged
/// lines before and after each change. Two changes share a hunk when at
/// most 2 * context unchanged lines separate them, so hunks never overlap
/// or touch. An all-Equal script gives no hunks.
std::vector<Hunk> make_hunks(const EditScript& script, std::size_t context = kDefaultContext);

}  // namespace diffcore
