// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.
//
// The patch model shared by the unified diff writer, parser and applier.

#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "diffcore/diff.hpp"
#include "diffcore/hunks.hpp"
#include "diffcore/lines.hpp"

namespace diffcore {

/// Line numbers and counts in a patch must not exceed this, so offsets
/// between positions always fit in std::ptrdiff_t.
inline constexpr std::size_t kMaxLineNumber =
    static_cast<std::size_t>((std::numeric_limits<std::ptrdiff_t>::max)() / 4);

enum class LineKind : unsigned char { Context, Removed, Added };

struct PatchLine {
    LineKind kind = LineKind::Context;
    /// The line without its prefix character and '\n'.
    std::string text;
    /// The line has no '\n' ("\ No newline at end of file" follows it).
    /// Only the last line of a hunk's old or new text may have this set.
    bool no_newline = false;
};

struct PatchHunk {
    /// First old line (0-based); when the hunk has no old lines, the line
    /// its new lines go before. The header shows it 1-based, or for an empty
    /// range as the number of the line before it.
    std::size_t old_start = 0;
    /// First new line (0-based), as old_start.
    std::size_t new_start = 0;
    std::vector<PatchLine> lines;
    /// Text after the closing "@@" of the header, without the space before
    /// it (diff -p puts a function name there). Kept, not interpreted.
    std::string section;

    /// Context and removed lines.
    std::size_t old_count() const noexcept;
    /// Context and added lines.
    std::size_t new_count() const noexcept;
};

/// The changes to one file.
struct FilePatch {
    /// Everything after "--- " and "+++ ": a name, often followed by a tab
    /// and a timestamp.
    std::string old_label;
    std::string new_label;
    std::vector<PatchHunk> hunks;
};

/// A patch file: one or more file sections.
struct Patch {
    std::vector<FilePatch> files;
};

/// Builds the patch for hunks made from a diff of `old_lines` against
/// `new_lines`.
FilePatch make_file_patch(const Lines& old_lines, const Lines& new_lines, const std::vector<Hunk>& hunks,
                          std::string old_label, std::string new_label);

/// Diffs two texts and builds the patch: split_lines, diff, make_hunks and
/// make_file_patch in one call. No hunks when the texts are equal.
FilePatch make_patch(std::string_view old_text, std::string_view new_text, std::string old_label,
                     std::string new_label, const DiffOptions& options = {});

/// The patch that undoes `patch`: old and new swapped, removed lines added
/// and added lines removed.
FilePatch reversed(const FilePatch& patch);

/// The part of a label before its first tab: the file name when the label
/// carries a timestamp.
std::string_view label_path(std::string_view label) noexcept;

}  // namespace diffcore
