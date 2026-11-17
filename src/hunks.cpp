// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.

#include "diffcore/hunks.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace diffcore {

namespace {

// Whether `gap` unchanged lines are few enough for the changes on either
// side to share a hunk: at most 2 * context, without overflowing.
bool joins(std::size_t gap, std::size_t context) noexcept { return gap <= context || gap - context <= context; }

std::size_t at_most(std::size_t a, std::size_t b) noexcept { return a < b ? a : b; }

}  // namespace

std::vector<Hunk> make_hunks(const EditScript& script, std::size_t context) {
    std::vector<Hunk> hunks;
    const std::size_t size = script.size();
    std::size_t e = 0;
    while (e < size) {
        if (script[e].kind == EditKind::Equal) {
            ++e;
            continue;
        }
        Hunk hunk;
        // Runs are maximal, so the edit before a change is Equal.
        if (e > 0) {
            const Edit& before = script[e - 1];
            const std::size_t lead = at_most(context, before.count);
            if (lead > 0) {
                hunk.edits.push_back(Edit{EditKind::Equal, before.old_start + before.count - lead,
                                          before.new_start + before.count - lead, lead});
            }
        }
        std::size_t f = e;
        for (;;) {
            while (f < size && script[f].kind != EditKind::Equal) hunk.edits.push_back(script[f++]);
            const bool change_follows = f + 1 < size;
            if (f < size && change_follows && joins(script[f].count, context)) {
                hunk.edits.push_back(script[f++]);
                continue;
            }
            break;
        }
        if (f < size) {
            const Edit& after = script[f];
            const std::size_t trail = at_most(context, after.count);
            if (trail > 0) hunk.edits.push_back(Edit{EditKind::Equal, after.old_start, after.new_start, trail});
        }

        hunk.old_start = hunk.edits.front().old_start;
        hunk.new_start = hunk.edits.front().new_start;
        for (const Edit& edit : hunk.edits) {
            if (edit.kind != EditKind::Insert) hunk.old_count += edit.count;
            if (edit.kind != EditKind::Delete) hunk.new_count += edit.count;
        }
        hunks.push_back(std::move(hunk));
        e = f;
    }
    return hunks;
}

}  // namespace diffcore
