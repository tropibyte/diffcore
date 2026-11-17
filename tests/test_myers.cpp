// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "helpers.hpp"

using namespace diffcore;
using namespace diffcore::test;

namespace {

std::size_t distance(const EditScript& script) { return count_changes(script).distance(); }

EditScript diff_of(const char* a, const char* b) { return diff_tokens(tokens(a), tokens(b)); }

}  // namespace

TEST_CASE("the example from Myers' paper has distance 5") {
    const auto a = tokens("abcabba");
    const auto b = tokens("cbabac");
    const EditScript script = diff_tokens(a, b);
    CHECK(distance(script) == 5);
    CHECK(replay(script, a, b) == b);
}

TEST_CASE("trivial inputs") {
    CHECK(diff_of("", "").empty());
    CHECK(diff_of("abc", "abc") == EditScript{{EditKind::Equal, 0, 0, 3}});
    CHECK(diff_of("", "ab") == EditScript{{EditKind::Insert, 0, 0, 2}});
    CHECK(diff_of("ab", "") == EditScript{{EditKind::Delete, 0, 0, 2}});
    CHECK(diff_of("ab", "cd") == EditScript{{EditKind::Delete, 0, 0, 2}, {EditKind::Insert, 2, 0, 2}});
}

TEST_CASE("changes between a common prefix and suffix") {
    CHECK(diff_of("abXcd", "abYcd") == EditScript{{EditKind::Equal, 0, 0, 2},
                                                  {EditKind::Delete, 2, 2, 1},
                                                  {EditKind::Insert, 3, 2, 1},
                                                  {EditKind::Equal, 3, 3, 2}});
    CHECK(diff_of("abcd", "abXYcd") ==
          EditScript{{EditKind::Equal, 0, 0, 2}, {EditKind::Insert, 2, 2, 2}, {EditKind::Equal, 2, 4, 2}});
}

TEST_CASE("minimal scripts on small hand-made cases") {
    struct Case {
        const char* a;
        const char* b;
        std::size_t d;
    };
    const Case cases[] = {
        {"a", "b", 2},          {"ab", "ba", 2},        {"abc", "acb", 2},       {"aaaa", "aa", 2},
        {"abab", "baba", 2},    {"xaxbxc", "abc", 3},   {"abcdef", "fedcba", 10}, {"aabbcc", "abcabc", 4},
        {"abcxyz", "xyzabc", 6}, {"kitten", "sitting", 5},
    };
    for (const Case& c : cases) {
        CAPTURE(c.a);
        CAPTURE(c.b);
        const auto a = tokens(c.a);
        const auto b = tokens(c.b);
        const EditScript script = diff_tokens(a, b);
        CHECK(distance(script) == c.d);
        CHECK(replay(script, a, b) == b);
    }
}

TEST_CASE("scripts are minimal: random small inputs against a brute-force LCS") {
    docgen::Rng rng(20261117);
    for (int round = 0; round < 3000; ++round) {
        const std::size_t alphabet = 1 + rng.index(4);
        std::vector<TokenId> a(rng.index(13));
        std::vector<TokenId> b(rng.index(13));
        for (TokenId& t : a) t = static_cast<TokenId>(rng.index(alphabet));
        for (TokenId& t : b) t = static_cast<TokenId>(rng.index(alphabet));
        const EditScript script = diff_tokens(a, b);
        const std::size_t lcs = lcs_length(a, b);
        REQUIRE(distance(script) == a.size() + b.size() - 2 * lcs);
        REQUIRE(replay(script, a, b) == b);
    }
}

TEST_CASE("scripts are minimal: larger random documents") {
    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        CAPTURE(seed);
        const docgen::DocPair docs = random_docs(seed, 150, 0.2, 6);
        const Lines a = split_lines(docs.old_text);
        const Lines b = split_lines(docs.new_text);
        Interner interner;
        const std::vector<TokenId> ta = tokenize(a, interner);
        const std::vector<TokenId> tb = tokenize(b, interner);
        const EditScript script = diff(a, b);
        CHECK(distance(script) == ta.size() + tb.size() - 2 * lcs_length(ta, tb));
        CHECK(replay(script, ta, tb) == tb);
    }
}

TEST_CASE("runs of changes slide down and merge") {
    // An inserted copy of a repeated line is the last one.
    CHECK(diff_of("xx", "xxx") == EditScript{{EditKind::Equal, 0, 0, 2}, {EditKind::Insert, 2, 2, 1}});
    CHECK(diff_of("a", "baba") == EditScript{{EditKind::Insert, 0, 0, 3}, {EditKind::Equal, 0, 3, 1}});
    // Deletions merge into one block where they can.
    CHECK(diff_of("aba", "a") == EditScript{{EditKind::Equal, 0, 0, 1}, {EditKind::Delete, 1, 1, 2}});
    CHECK(diff_of("abxaby", "aby") ==
          EditScript{{EditKind::Equal, 0, 0, 2}, {EditKind::Delete, 2, 2, 3}, {EditKind::Equal, 5, 2, 1}});
    CHECK(diff_of("abcabc", "abc") == EditScript{{EditKind::Equal, 0, 0, 3}, {EditKind::Delete, 3, 3, 3}});
}

TEST_CASE("a replaced block keeps its removed and added lines together") {
    // Either "a" can be the deleted one; deleting the first puts the
    // deletion next to the insertion of "x", making one change of two.
    const EditScript script = diff_of("paaq", "pxaq");
    CHECK(distance(script) == 2);
    CHECK(script == EditScript{{EditKind::Equal, 0, 0, 1},
                               {EditKind::Delete, 1, 1, 1},
                               {EditKind::Insert, 2, 1, 1},
                               {EditKind::Equal, 2, 2, 2}});
}

TEST_CASE("the final newline is part of the last line") {
    const std::string with = lines_text({"a", "b"});
    const std::string without = "a\nb";
    const EditScript script = diff(split_lines(with), split_lines(without));
    CHECK(script == EditScript{{EditKind::Equal, 0, 0, 1}, {EditKind::Delete, 1, 1, 1}, {EditKind::Insert, 2, 1, 1}});
    CHECK(diff(split_lines(without), split_lines(without)) == EditScript{{EditKind::Equal, 0, 0, 2}});
}

TEST_CASE("the cost limit stops the search with an error") {
    std::vector<TokenId> a;
    std::vector<TokenId> b;
    for (TokenId i = 0; i < 100; ++i) {
        a.push_back(i);
        b.push_back(i + 1000);
    }
    CHECK_THROWS_AS(diff_tokens(a, b, 50), LimitError);
    CHECK_THROWS_WITH(diff_tokens(a, b, 199), "edit distance exceeds the limit of 199 lines");
    CHECK(distance(diff_tokens(a, b, 200)) == 200);

    DiffOptions options;
    options.max_cost = 3;
    const std::string old_text = numbered(20);
    std::string new_text = old_text;
    new_text.replace(new_text.find("5\n"), 2, "five\n");
    new_text.replace(new_text.find("15\n"), 3, "fifteen\n");
    CHECK_THROWS_AS(diff(split_lines(old_text), split_lines(new_text), options), LimitError);
    options.max_cost = 4;
    CHECK(distance(diff(split_lines(old_text), split_lines(new_text), options)) == 4);
}

TEST_CASE("no search, so no limit, when one side is only trimmed away") {
    std::vector<TokenId> a{1, 2, 3};
    std::vector<TokenId> b{1, 2};
    for (TokenId i = 0; i < 5000; ++i) b.push_back(100 + i);
    b.push_back(3);
    CHECK(distance(diff_tokens(a, b, 0)) == 5000);
    CHECK(distance(diff_tokens(b, a, 0)) == 5000);
    CHECK(diff_tokens(a, a, 0) == EditScript{{EditKind::Equal, 0, 0, 3}});
}

TEST_CASE("count_changes adds up deletions and insertions") {
    const ChangeCounts counts = count_changes(diff_of("abcdef", "axcyzf"));
    CHECK(counts.deleted == 3);
    CHECK(counts.inserted == 3);
    CHECK(counts.distance() == 6);
    CHECK(count_changes(EditScript{}).distance() == 0);
}
