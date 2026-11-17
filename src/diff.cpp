// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.

#include "diffcore/diff.hpp"

#include <vector>

#include "diffcore/tokens.hpp"

namespace diffcore {

EditScript diff(const Lines& old_lines, const Lines& new_lines, const DiffOptions& options) {
    Interner interner;
    const std::vector<TokenId> a = tokenize(old_lines, interner);
    const std::vector<TokenId> b = tokenize(new_lines, interner);
    return diff_tokens(a, b, options.max_cost);
}

ChangeCounts count_changes(const EditScript& script) noexcept {
    ChangeCounts counts;
    for (const Edit& e : script) {
        if (e.kind == EditKind::Delete) counts.deleted += e.count;
        if (e.kind == EditKind::Insert) counts.inserted += e.count;
    }
    return counts;
}

}  // namespace diffcore
