// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "helpers.hpp"

using namespace diffcore;
using namespace diffcore::test;

namespace {

// numbered(n) with the given 1-based lines replaced by "x<line>".
std::string changed(std::size_t n, std::vector<std::size_t> lines) {
    std::string out;
    for (std::size_t i = 1; i <= n; ++i) {
        bool hit = false;
        for (const std::size_t l : lines) hit = hit || l == i;
        out += (hit ? "x" : "") + std::to_string(i) + '\n';
    }
    return out;
}

std::vector<Hunk> hunks_of(const std::string& a, const std::string& b, std::size_t context = 3) {
    return make_hunks(diff(split_lines(a), split_lines(b)), context);
}

}  // namespace

TEST_CASE("no changes, no hunks") {
    CHECK(make_hunks(EditScript{}).empty());
    CHECK(make_hunks(EditScript{{EditKind::Equal, 0, 0, 10}}).empty());
}

TEST_CASE("a change gets three lines of context on each side") {
    const auto hunks = hunks_of(numbered(20), changed(20, {10}));
    REQUIRE(hunks.size() == 1);
    const Hunk& h = hunks[0];
    CHECK(h.old_start == 6);
    CHECK(h.old_count == 7);
    CHECK(h.new_start == 6);
    CHECK(h.new_count == 7);
    CHECK(h.edits == EditScript{{EditKind::Equal, 6, 6, 3},
                                {EditKind::Delete, 9, 9, 1},
                                {EditKind::Insert, 10, 9, 1},
                                {EditKind::Equal, 10, 10, 3}});
}

TEST_CASE("changes up to twice the context apart share a hunk") {
    // Six unchanged lines between: the contexts meet.
    auto hunks = hunks_of(numbered(30), changed(30, {10, 17}));
    REQUIRE(hunks.size() == 1);
    CHECK(hunks[0].old_start == 6);
    CHECK(hunks[0].old_count == 14);
    CHECK(hunks[0].edits.size() == 7);

    // Seven: one line between the contexts stays out.
    hunks = hunks_of(numbered(30), changed(30, {10, 18}));
    REQUIRE(hunks.size() == 2);
    CHECK(hunks[0].old_start == 6);
    CHECK(hunks[0].old_count == 7);
    CHECK(hunks[1].old_start == 14);
    CHECK(hunks[1].old_count == 7);

    hunks = hunks_of(numbered(30), changed(30, {10, 14}), 1);
    CHECK(hunks.size() == 2);
    hunks = hunks_of(numbered(30), changed(30, {10, 13}), 1);
    CHECK(hunks.size() == 1);
}

TEST_CASE("context is cut short at the ends of the file") {
    auto hunks = hunks_of(numbered(10), changed(10, {1}));
    REQUIRE(hunks.size() == 1);
    CHECK(hunks[0].old_start == 0);
    CHECK(hunks[0].old_count == 4);
    CHECK(hunks[0].edits.front().kind == EditKind::Delete);

    hunks = hunks_of(numbered(10), changed(10, {10}));
    REQUIRE(hunks.size() == 1);
    CHECK(hunks[0].old_start == 6);
    CHECK(hunks[0].old_count == 4);
    CHECK(hunks[0].edits.back().kind == EditKind::Insert);

    hunks = hunks_of(numbered(10), changed(10, {2, 9}));
    REQUIRE(hunks.size() == 1);
    CHECK(hunks[0].old_count == 10);
}

TEST_CASE("without context every change is its own hunk") {
    const std::string a = lines_text({"a", "b", "c", "d"});
    const std::string b = lines_text({"a", "B", "c", "d", "e"});
    const auto hunks = hunks_of(a, b, 0);
    REQUIRE(hunks.size() == 2);
    CHECK(hunks[0].old_start == 1);
    CHECK(hunks[0].old_count == 1);
    CHECK(hunks[0].new_count == 1);
    // Pure insertion: no old lines, positioned before old line 4 (end).
    CHECK(hunks[1].old_start == 4);
    CHECK(hunks[1].old_count == 0);
    CHECK(hunks[1].new_start == 4);
    CHECK(hunks[1].new_count == 1);
}

TEST_CASE("a huge context makes one hunk of the whole file") {
    const auto hunks = hunks_of(numbered(50), changed(50, {3, 47}), (std::numeric_limits<std::size_t>::max)());
    REQUIRE(hunks.size() == 1);
    CHECK(hunks[0].old_start == 0);
    CHECK(hunks[0].old_count == 50);
    CHECK(hunks[0].new_count == 50);
}

TEST_CASE("hunk counts agree with their edits") {
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        const docgen::DocPair docs = random_docs(seed, 80, 0.1, 0);
        const EditScript script = diff(split_lines(docs.old_text), split_lines(docs.new_text));
        std::size_t previous_end = 0;
        for (const Hunk& h : make_hunks(script, 2)) {
            std::size_t old_count = 0;
            std::size_t new_count = 0;
            for (const Edit& e : h.edits) {
                if (e.kind != EditKind::Insert) old_count += e.count;
                if (e.kind != EditKind::Delete) new_count += e.count;
            }
            CHECK(h.old_count == old_count);
            CHECK(h.new_count == new_count);
            CHECK(h.old_start >= previous_end);
            const bool lead_ok = h.edits.front().kind != EditKind::Equal || h.edits.front().count <= 2;
            const bool trail_ok = h.edits.back().kind != EditKind::Equal || h.edits.back().count <= 2;
            CHECK(lead_ok);
            CHECK(trail_ok);
            previous_end = h.old_start + h.old_count;
        }
    }
}
