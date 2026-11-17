// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.

#include "diffcore/patch.hpp"

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace diffcore {

std::size_t PatchHunk::old_count() const noexcept {
    std::size_t count = 0;
    for (const PatchLine& line : lines) {
        if (line.kind != LineKind::Added) ++count;
    }
    return count;
}

std::size_t PatchHunk::new_count() const noexcept {
    std::size_t count = 0;
    for (const PatchLine& line : lines) {
        if (line.kind != LineKind::Removed) ++count;
    }
    return count;
}

FilePatch make_file_patch(const Lines& old_lines, const Lines& new_lines, const std::vector<Hunk>& hunks,
                          std::string old_label, std::string new_label) {
    FilePatch patch;
    patch.old_label = std::move(old_label);
    patch.new_label = std::move(new_label);
    for (const Hunk& hunk : hunks) {
        PatchHunk out;
        out.old_start = hunk.old_start;
        out.new_start = hunk.new_start;
        out.lines.reserve(hunk.old_count + hunk.new_count);
        for (const Edit& edit : hunk.edits) {
            for (std::size_t t = 0; t < edit.count; ++t) {
                const std::size_t i = edit.old_start + t;
                const std::size_t j = edit.new_start + t;
                switch (edit.kind) {
                    case EditKind::Equal:
                        out.lines.push_back(PatchLine{LineKind::Context, std::string(old_lines[i]),
                                                      old_lines.is_incomplete(i)});
                        break;
                    case EditKind::Delete:
                        out.lines.push_back(PatchLine{LineKind::Removed, std::string(old_lines[i]),
                                                      old_lines.is_incomplete(i)});
                        break;
                    case EditKind::Insert:
                        out.lines.push_back(PatchLine{LineKind::Added, std::string(new_lines[j]),
                                                      new_lines.is_incomplete(j)});
                        break;
                }
            }
        }
        patch.hunks.push_back(std::move(out));
    }
    return patch;
}

FilePatch make_patch(std::string_view old_text, std::string_view new_text, std::string old_label,
                     std::string new_label, const DiffOptions& options) {
    const Lines a = split_lines(old_text);
    const Lines b = split_lines(new_text);
    const EditScript script = diff(a, b, options);
    return make_file_patch(a, b, make_hunks(script, options.context), std::move(old_label), std::move(new_label));
}

FilePatch reversed(const FilePatch& patch) {
    FilePatch out;
    out.old_label = patch.new_label;
    out.new_label = patch.old_label;
    out.hunks.reserve(patch.hunks.size());
    for (const PatchHunk& hunk : patch.hunks) {
        PatchHunk r;
        r.old_start = hunk.new_start;
        r.new_start = hunk.old_start;
        r.section = hunk.section;
        r.lines.reserve(hunk.lines.size());
        // Keep removed-before-added order within each change.
        std::size_t i = 0;
        while (i < hunk.lines.size()) {
            if (hunk.lines[i].kind == LineKind::Context) {
                r.lines.push_back(hunk.lines[i++]);
                continue;
            }
            std::size_t end = i;
            while (end < hunk.lines.size() && hunk.lines[end].kind != LineKind::Context) ++end;
            for (const LineKind wanted : {LineKind::Added, LineKind::Removed}) {
                for (std::size_t t = i; t < end; ++t) {
                    if (hunk.lines[t].kind != wanted) continue;
                    PatchLine line = hunk.lines[t];
                    line.kind = wanted == LineKind::Added ? LineKind::Removed : LineKind::Added;
                    r.lines.push_back(std::move(line));
                }
            }
            i = end;
        }
        out.hunks.push_back(std::move(r));
    }
    return out;
}

std::string_view label_path(std::string_view label) noexcept { return label.substr(0, label.find('\t')); }

}  // namespace diffcore
