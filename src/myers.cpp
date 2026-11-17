// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.
//
// Myers' greedy O(ND) shortest edit script search ("An O(ND) Difference
// Algorithm and Its Variations", Algorithmica 1, 1986), section 3, with the
// frontier of every step kept for the backtrack, followed by a pass that
// slides runs of changes into their conventional positions.

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "diffcore/diff.hpp"
#include "diffcore/error.hpp"

namespace diffcore {

namespace {

using Pos = std::ptrdiff_t;
using Flags = std::vector<unsigned char>;
using Frontier = std::vector<std::uint32_t>;

// Frontier entries are at most n + 2m, stored in 32 bits; this bound also
// keeps every position within a 32-bit std::ptrdiff_t.
constexpr std::size_t kMaxSearchLines = (std::size_t{1} << 30) - 1;

Pos entry(const Frontier& f, Pos i) { return static_cast<Pos>(f[static_cast<std::size_t>(i)]); }

// Finds a shortest edit script between a[0, n) and b[0, m), both non-empty,
// and sets del[x] for each deleted a[x] and ins[y] for each inserted b[y].
//
// Diagonal k holds the points with x - y = k. After d non-diagonal moves
// the furthest point reachable on diagonal k (k = -d, -d + 2, ..., d) is
// frontier[d][(k + d) / 2]. A point is reached either by a move down from
// diagonal k + 1 (inserting b[y]) or right from diagonal k - 1 (deleting
// a[x]), then by following equal lines. The grid is treated as unbounded
// to the right and below, where no lines are equal; points out there can
// never lead back to (n, m), so they need no special casing.
void myers(const TokenId* a, std::size_t n, const TokenId* b, std::size_t m, std::size_t max_cost,
           unsigned char* del, unsigned char* ins) {
    if (n + m > kMaxSearchLines) {
        throw LimitError("inputs too large to diff: more than " + std::to_string(kMaxSearchLines) +
                         " differing lines in total");
    }
    const Pos N = static_cast<Pos>(n);
    const Pos M = static_cast<Pos>(m);

    std::vector<Frontier> frontier;
    Pos found = -1;
    for (Pos d = 0; found < 0; ++d) {
        if (static_cast<std::size_t>(d) > max_cost) {
            throw LimitError("edit distance exceeds the limit of " + std::to_string(max_cost) + " lines");
        }
        Frontier cur(static_cast<std::size_t>(d) + 1);
        for (Pos i = 0; i <= d; ++i) {
            const Pos k = 2 * i - d;
            Pos x = 0;
            if (d > 0) {
                // In the previous step, diagonal k + 1 is at index i and
                // k - 1 at index i - 1.
                const Frontier& prev = frontier.back();
                if (k == -d || (k != d && entry(prev, i - 1) < entry(prev, i))) {
                    x = entry(prev, i);
                } else {
                    x = entry(prev, i - 1) + 1;
                }
            }
            Pos y = x - k;
            while (x < N && y < M && a[x] == b[y]) {
                ++x;
                ++y;
            }
            cur[static_cast<std::size_t>(i)] = static_cast<std::uint32_t>(x);
            if (x >= N && y >= M) {
                found = d;
                break;
            }
        }
        frontier.push_back(std::move(cur));
    }

    // Walk back from (n, m), repeating each step's choice of move.
    Pos x = N;
    Pos y = M;
    for (Pos d = found; d > 0; --d) {
        const Frontier& prev = frontier[static_cast<std::size_t>(d - 1)];
        const Pos k = x - y;
        const Pos i = (k + d) / 2;
        if (k == -d || (k != d && entry(prev, i - 1) < entry(prev, i))) {
            x = entry(prev, i);
            y = x - (k + 1);
            ins[y] = 1;
        } else {
            x = entry(prev, i - 1);
            y = x - (k - 1);
            del[x] = 1;
        }
    }
}

// gaps[g] is set when the file has changed lines between its g-th and
// (g + 1)-th unchanged lines (gap 0 is before the first unchanged line).
// Unchanged lines pair up in order between the two files, so gap g of one
// file faces gap g of the other.
Flags changed_gaps(const Flags& changed) {
    Flags gaps(1, 0);
    for (const unsigned char c : changed) {
        if (c) {
            gaps.back() = 1;
        } else {
            gaps.push_back(0);
        }
    }
    return gaps;
}

// A run of changed lines can trade places with an equal unchanged line next
// to it without changing the edit distance: if the line after the run
// equals its first line, the run can move down one line, and likewise up.
// Each run is moved up and down as far as it goes, absorbing the runs it
// meets, until it stops growing. It then rests at the lowest position it
// can reach that faces a change in the other file, so a replaced block
// prints as removed lines followed by added lines; with no such position,
// at the lowest position of all.
void compact(Flags& changed, const std::vector<TokenId>& ids, const Flags& other_changed) {
    const std::size_t n = ids.size();
    const Flags other_gaps = changed_gaps(other_changed);
    std::size_t i = 0;
    std::size_t gap = 0;  // unchanged lines before i
    for (;;) {
        while (i < n && !changed[i]) {
            ++i;
            ++gap;
        }
        if (i == n) break;

        std::size_t start = i;
        std::size_t end = i;
        while (end < n && changed[end]) ++end;

        std::size_t top_gap = gap;
        for (;;) {
            const std::size_t length = end - start;
            while (start > 0 && ids[start - 1] == ids[end - 1]) {
                changed[--start] = 1;
                changed[--end] = 0;
                --gap;
                while (start > 0 && changed[start - 1]) --start;
            }
            top_gap = gap;
            while (end < n && ids[start] == ids[end]) {
                changed[start++] = 0;
                changed[end++] = 1;
                ++gap;
                while (end < n && changed[end]) ++end;
            }
            if (end - start == length) break;
        }

        // The last pass met no other run, so every position between the
        // top and here is free.
        std::size_t back = 0;
        while (gap - back > top_gap && !other_gaps[gap - back]) ++back;
        if (other_gaps[gap - back]) {
            for (; back > 0; --back) {
                changed[--start] = 1;
                changed[--end] = 0;
                --gap;
            }
        }
        i = end;
    }
}

EditScript script_from_flags(const Flags& del, const Flags& ins) {
    EditScript script;
    const std::size_t n = del.size();
    const std::size_t m = ins.size();
    std::size_t i = 0;
    std::size_t j = 0;
    while (i < n || j < m) {
        const std::size_t i0 = i;
        const std::size_t j0 = j;
        while (i < n && j < m && !del[i] && !ins[j]) {
            ++i;
            ++j;
        }
        if (i > i0) script.push_back(Edit{EditKind::Equal, i0, j0, i - i0});
        const std::size_t del_start = i;
        while (i < n && del[i]) ++i;
        if (i > del_start) script.push_back(Edit{EditKind::Delete, del_start, j, i - del_start});
        const std::size_t ins_start = j;
        while (j < m && ins[j]) ++j;
        if (j > ins_start) script.push_back(Edit{EditKind::Insert, i, ins_start, j - ins_start});
        if (i == i0 && j == j0) throw std::logic_error("diffcore: inconsistent change flags");
    }
    return script;
}

}  // namespace

EditScript diff_tokens(const std::vector<TokenId>& a, const std::vector<TokenId>& b, std::size_t max_cost) {
    const std::size_t n = a.size();
    const std::size_t m = b.size();
    std::size_t prefix = 0;
    while (prefix < n && prefix < m && a[prefix] == b[prefix]) ++prefix;
    std::size_t suffix = 0;
    while (suffix < n - prefix && suffix < m - prefix && a[n - 1 - suffix] == b[m - 1 - suffix]) ++suffix;

    Flags del(n, 0);
    Flags ins(m, 0);
    const std::size_t mid_n = n - prefix - suffix;
    const std::size_t mid_m = m - prefix - suffix;
    if (mid_n == 0) {
        for (std::size_t j = prefix; j < prefix + mid_m; ++j) ins[j] = 1;
    } else if (mid_m == 0) {
        for (std::size_t i = prefix; i < prefix + mid_n; ++i) del[i] = 1;
    } else {
        myers(a.data() + prefix, mid_n, b.data() + prefix, mid_m, max_cost, del.data() + prefix, ins.data() + prefix);
    }
    compact(del, a, ins);
    compact(ins, b, del);
    return script_from_flags(del, ins);
}

}  // namespace diffcore
