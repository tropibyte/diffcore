// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.

#include <cstddef>
#include <string>
#include <string_view>

#include "diffcore/patch.hpp"
#include "diffcore/unified.hpp"

namespace diffcore {

namespace {

constexpr std::string_view kNoNewlineMarker = "\\ No newline at end of file\n";

char prefix_of(LineKind kind) noexcept {
    switch (kind) {
        case LineKind::Removed:
            return '-';
        case LineKind::Added:
            return '+';
        case LineKind::Context:
            break;
    }
    return ' ';
}

void append_file(std::string& out, const FilePatch& patch) {
    if (patch.hunks.empty()) return;
    out += "--- ";
    out += patch.old_label;
    out += "\n+++ ";
    out += patch.new_label;
    out += '\n';
    for (const PatchHunk& hunk : patch.hunks) {
        out += format_hunk_header(hunk);
        out += '\n';
        for (const PatchLine& line : hunk.lines) {
            out += prefix_of(line.kind);
            out += line.text;
            out += '\n';
            if (line.no_newline) out += kNoNewlineMarker;
        }
    }
}

}  // namespace

std::string format_range(std::size_t start, std::size_t count) {
    if (count == 0) return std::to_string(start) + ",0";
    if (count == 1) return std::to_string(start + 1);
    return std::to_string(start + 1) + "," + std::to_string(count);
}

std::string format_hunk_header(const PatchHunk& hunk) {
    std::string header = "@@ -" + format_range(hunk.old_start, hunk.old_count()) + " +" +
                         format_range(hunk.new_start, hunk.new_count()) + " @@";
    if (!hunk.section.empty()) {
        header += ' ';
        header += hunk.section;
    }
    return header;
}

std::string format_unified(const FilePatch& patch) {
    std::string out;
    append_file(out, patch);
    return out;
}

std::string format_unified(const Patch& patch) {
    std::string out;
    for (const FilePatch& file : patch.files) append_file(out, file);
    return out;
}

std::string unified_diff(std::string_view old_text, std::string_view new_text, std::string_view old_label,
                         std::string_view new_label, const DiffOptions& options) {
    return format_unified(make_patch(old_text, new_text, std::string(old_label), std::string(new_label), options));
}

}  // namespace diffcore
