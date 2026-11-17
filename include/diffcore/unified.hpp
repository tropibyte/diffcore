// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.
//
// Writing and reading the unified diff format.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "diffcore/diff.hpp"
#include "diffcore/patch.hpp"

namespace diffcore {

/// One side of a hunk header, given a 0-based start and a count: "5,3" for
/// lines 5-7, "5" for line 5 alone, and for an empty range the number of
/// the line before it, "4,0" (so "0,0" at the start of a file).
std::string format_range(std::size_t start, std::size_t count);

/// "@@ -old +new @@", followed by " section" if the hunk has one.
std::string format_hunk_header(const PatchHunk& hunk);

/// The "--- " and "+++ " lines, then each hunk. Every line ends with '\n';
/// a line flagged no_newline is followed by "\ No newline at end of file".
/// Empty when the patch has no hunks.
std::string format_unified(const FilePatch& patch);
std::string format_unified(const Patch& patch);

/// Diffs two texts and formats the result: `diff -u` output with the given
/// labels. Empty when the texts are equal. Throws LimitError (see
/// diff_tokens).
std::string unified_diff(std::string_view old_text, std::string_view new_text, std::string_view old_label,
                         std::string_view new_label, const DiffOptions& options = {});

/// Parses a unified diff. Lines before a "--- " / "+++ " header pair, such
/// as "diff --git" and "index" lines or mail headers, are skipped, as is
/// anything after a file's last hunk that does not start another hunk.
/// Inside a hunk, an empty line is read as an empty context line (some
/// tools strip the leading space).
///
/// Throws ParseError, with the patch line number, for a "--- " line without
/// "+++ " after it, a file header without hunks, a hunk header that does not
/// parse or appears outside a file section, hunk text that does not agree
/// with the header's counts, a misplaced "\ No newline at end of file", and
/// hunks out of order. A text with no file sections gives an empty Patch.
Patch parse_patch(std::string_view text);

}  // namespace diffcore
