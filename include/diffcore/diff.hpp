// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.
//
// Minimal line diffs: Myers' O(ND) algorithm over interned lines.

#pragma once

#include <cstddef>
#include <vector>

#include "diffcore/lines.hpp"
#include "diffcore/tokens.hpp"

namespace diffcore {

enum class EditKind : unsigned char { Equal, Insert, Delete };

/// A run of lines that are equal in both texts, inserted, or deleted.
struct Edit {
    EditKind kind = EditKind::Equal;
    /// First old line (0-based). For an Insert: the old line the inserted
    /// lines come before.
    std::size_t old_start = 0;
    /// First new line (0-based). For a Delete: the new line the deleted
    /// lines came before.
    std::size_t new_start = 0;
    std::size_t count = 0;

    friend bool operator==(const Edit& a, const Edit& b) noexcept {
        return a.kind == b.kind && a.old_start == b.old_start && a.new_start == b.new_start && a.count == b.count;
    }
    friend bool operator!=(const Edit& a, const Edit& b) noexcept { return !(a == b); }
};

/// Edits in order, covering both texts from start to end. Runs are maximal
/// (no two neighbours have the same kind) and none is empty; where lines
/// are both deleted and inserted, the Delete comes first.
using EditScript = std::vector<Edit>;

inline constexpr std::size_t kDefaultContext = 3;
inline constexpr std::size_t kDefaultMaxCost = 10000;

struct DiffOptions {
    /// Lines of context around each change in a unified diff.
    std::size_t context = kDefaultContext;
    /// Most edit steps (inserted plus deleted lines) the search may explore.
    /// The search keeps every step's frontier for the backtrack, about
    /// 2 * max_cost^2 bytes at the limit (200 MB for the default).
    std::size_t max_cost = kDefaultMaxCost;
};

/// A minimal edit script turning `a` into `b`: as few inserted plus deleted
/// tokens as possible. Common leading and trailing tokens are set aside
/// before the search. Among the minimal scripts, runs of changes are moved
/// down past equal tokens where possible and merged where they can be, which
/// is what diff readers expect: inserting "x" into "x x" shows the last "x"
/// as the new one.
///
/// Throws LimitError if what remains after trimming needs a search of more
/// than `max_cost` steps. When one side is empty after trimming, no search
/// is needed and there is no limit.
EditScript diff_tokens(const std::vector<TokenId>& a, const std::vector<TokenId>& b,
                       std::size_t max_cost = kDefaultMaxCost);

/// diff_tokens over interned lines. A last line without '\n' differs from
/// the same text with one.
EditScript diff(const Lines& old_lines, const Lines& new_lines, const DiffOptions& options = {});

struct ChangeCounts {
    std::size_t deleted = 0;
    std::size_t inserted = 0;
    /// The edit distance D.
    std::size_t distance() const noexcept { return deleted + inserted; }
};

ChangeCounts count_changes(const EditScript& script) noexcept;

}  // namespace diffcore
