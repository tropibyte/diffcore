// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.

#include "diffcore/apply.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "diffcore/lines.hpp"
#include "diffcore/patch.hpp"

namespace diffcore {

namespace {

using Side = std::vector<const PatchLine*>;

struct Sides {
    Side old_side;  // context and removed lines
    Side new_side;  // context and added lines
    std::size_t leading = 0;   // context lines before the first change
    std::size_t trailing = 0;  // context lines after the last change
};

Sides sides_of(const PatchHunk& hunk) {
    Sides s;
    for (const PatchLine& line : hunk.lines) {
        if (line.kind != LineKind::Added) s.old_side.push_back(&line);
        if (line.kind != LineKind::Removed) s.new_side.push_back(&line);
    }
    const std::size_t size = hunk.lines.size();
    while (s.leading < size && hunk.lines[s.leading].kind == LineKind::Context) ++s.leading;
    while (s.trailing < size - s.leading && hunk.lines[size - 1 - s.trailing].kind == LineKind::Context) ++s.trailing;
    if (s.leading == size) s.trailing = size;  // no changes at all: anchored at neither end
    return s;
}

// Only a side's last line may lack its newline.
bool well_formed(const Side& side) {
    for (std::size_t i = 0; i + 1 < side.size(); ++i) {
        if (side[i]->no_newline) return false;
    }
    return true;
}

bool ends_without_newline(const Side& side) { return !side.empty() && side.back()->no_newline; }

class Locator {
public:
    Locator(const Lines& in, const Sides& sides) : in_(in), sides_(sides) {}

    // Where the hunk applies, searching outwards from `guess` (which may lie
    // outside the text) and never below `lowest`.
    std::optional<std::size_t> find(const PatchHunk& hunk, std::ptrdiff_t guess, std::size_t lowest) const {
        const std::size_t n = in_.size();
        const std::size_t len = sides_.old_side.size();
        if (len > n) return std::nullopt;
        if (len == 0) {
            if (guess < 0) return std::nullopt;
            return try_at(static_cast<std::size_t>(guess), lowest);
        }
        if (sides_.trailing < sides_.leading) return try_at(n - len, lowest);
        if (sides_.leading < sides_.trailing && hunk.old_start == 0) return try_at(0, lowest);

        const auto lo = static_cast<std::ptrdiff_t>(lowest);
        const auto hi = static_cast<std::ptrdiff_t>(n - len);
        if (lo > hi) return std::nullopt;
        std::ptrdiff_t distance = 0;
        if (guess < lo) distance = lo - guess;
        if (guess > hi) distance = guess - hi;
        for (; guess + distance <= hi || guess - distance >= lo; ++distance) {
            const std::ptrdiff_t below = guess + distance;
            if (below >= lo && below <= hi) {
                if (const auto pos = try_at(static_cast<std::size_t>(below), lowest)) return pos;
            }
            const std::ptrdiff_t above = guess - distance;
            if (distance > 0 && above >= lo && above <= hi) {
                if (const auto pos = try_at(static_cast<std::size_t>(above), lowest)) return pos;
            }
        }
        return std::nullopt;
    }

private:
    std::optional<std::size_t> try_at(std::size_t pos, std::size_t lowest) const {
        const std::size_t n = in_.size();
        const Side& old_side = sides_.old_side;
        if (pos < lowest || pos > n || old_side.size() > n - pos) return std::nullopt;
        const bool reaches_end = pos + old_side.size() == n;
        if (ends_without_newline(sides_.new_side) && !reaches_end) return std::nullopt;
        // Nothing can follow a last line that has no newline.
        if (old_side.empty() && pos == n && in_.missing_final_newline && !sides_.new_side.empty()) {
            return std::nullopt;
        }
        for (std::size_t t = 0; t < old_side.size(); ++t) {
            const PatchLine& line = *old_side[t];
            if (in_[pos + t] != line.text || in_.is_incomplete(pos + t) != line.no_newline) return std::nullopt;
        }
        return pos;
    }

    const Lines& in_;
    const Sides& sides_;
};

std::size_t one_based(std::ptrdiff_t zero_based) {
    return zero_based < 0 ? 1 : static_cast<std::size_t>(zero_based) + 1;
}

}  // namespace

bool ApplyResult::ok() const noexcept { return failed_count() == 0; }

std::size_t ApplyResult::failed_count() const noexcept {
    std::size_t failed = 0;
    for (const HunkResult& h : hunks) {
        if (!h.applied) ++failed;
    }
    return failed;
}

ApplyResult apply_patch(std::string_view text, const FilePatch& patch, const ApplyOptions& options) {
    const FilePatch reverse_patch = options.reverse ? reversed(patch) : FilePatch{};
    const FilePatch& p = options.reverse ? reverse_patch : patch;
    const Lines in = split_lines(text);

    struct Placement {
        const PatchHunk* hunk;
        std::size_t pos;
        std::size_t old_len;
    };
    std::vector<Placement> placements;
    ApplyResult result;
    std::ptrdiff_t offset = 0;  // of the last hunk that applied
    std::ptrdiff_t growth = 0;  // lines added minus lines removed so far
    std::size_t lowest = 0;     // first line no earlier hunk matched

    for (const PatchHunk& hunk : p.hunks) {
        const Sides sides = sides_of(hunk);
        const bool in_range = hunk.old_start <= kMaxLineNumber;
        const auto stated = static_cast<std::ptrdiff_t>(in_range ? hunk.old_start : kMaxLineNumber);
        HunkResult r;
        std::optional<std::size_t> pos;
        if (in_range && well_formed(sides.old_side) && well_formed(sides.new_side)) {
            pos = Locator(in, sides).find(hunk, stated + offset, lowest);
        }
        if (pos) {
            const auto at = static_cast<std::ptrdiff_t>(*pos);
            r.applied = true;
            r.offset = at - stated;
            r.line = one_based(at + growth);
            offset = r.offset;
            lowest = *pos + sides.old_side.size();
            growth += static_cast<std::ptrdiff_t>(sides.new_side.size()) - static_cast<std::ptrdiff_t>(sides.old_side.size());
            placements.push_back(Placement{&hunk, *pos, sides.old_side.size()});
        } else {
            r.line = one_based(stated + growth);
        }
        result.hunks.push_back(r);
    }

    if (!result.ok() || options.dry_run) return result;

    // Byte offset of the start of line i (the end of the text for i == n).
    auto line_offset = [&](std::size_t i) {
        return i < in.size() ? static_cast<std::size_t>(in[i].data() - text.data()) : text.size();
    };
    std::string& out = result.output;
    out.reserve(text.size());
    std::size_t cursor = 0;
    for (const Placement& place : placements) {
        const std::size_t from = line_offset(cursor);
        out.append(text.substr(from, line_offset(place.pos) - from));
        for (const PatchLine& line : place.hunk->lines) {
            if (line.kind == LineKind::Removed) continue;
            out += line.text;
            if (!line.no_newline) out += '\n';
        }
        cursor = place.pos + place.old_len;
    }
    out.append(text.substr(line_offset(cursor)));
    return result;
}

}  // namespace diffcore
