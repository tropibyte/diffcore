// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.
//
// Applying a patch to a text.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "diffcore/patch.hpp"

namespace diffcore {

struct ApplyOptions {
    /// Apply the patch backwards, turning its new text into its old text.
    bool reverse = false;
    /// Locate every hunk but build no output.
    bool dry_run = false;
};

struct HunkResult {
    bool applied = false;
    /// 1-based line of the output where the hunk's text starts. For a hunk
    /// that failed, where it would have started with no offset.
    std::size_t line = 0;
    /// How far from its stated position the hunk applied, in lines
    /// (positive: further down the file).
    std::ptrdiff_t offset = 0;
};

struct ApplyResult {
    /// One result per hunk, in patch order.
    std::vector<HunkResult> hunks;
    /// The patched text, when every hunk applied and dry_run is off.
    /// Otherwise empty: the text is changed completely or not at all.
    std::string output;

    bool ok() const noexcept;
    std::size_t failed_count() const noexcept;
};

/// Applies the hunks of `patch` to `text`, in order. A hunk applies where
/// its old text (context and removed lines) matches exactly, including
/// whether the last line has a newline. It is tried at its stated line
/// first, shifted by the offset at which the previous hunk applied, then at
/// increasing distances from there, below before above, but never over
/// lines an earlier hunk matched. As with patch(1) at fuzz 0:
///   - a hunk with less context after its changes than before them only
///     applies at the end of the text;
///   - one with less context before its changes than after them, stated at
///     line 1, only applies at the start;
///   - a hunk with no old lines is not searched for: it applies at its
///     shifted position if that lies within the text.
/// A hunk whose new text ends without a newline must reach the end of the
/// text, and a hunk cannot add lines after a last line that has no newline.
/// A hunk that fails leaves the offset unchanged for the ones after it.
ApplyResult apply_patch(std::string_view text, const FilePatch& patch, const ApplyOptions& options = {});

}  // namespace diffcore
