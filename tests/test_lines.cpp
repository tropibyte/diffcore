// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "helpers.hpp"

using namespace diffcore;
using namespace diffcore::test;

TEST_CASE("an empty text has no lines") {
    const Lines lines = split_lines("");
    CHECK(lines.empty());
    CHECK_FALSE(lines.missing_final_newline);
}

TEST_CASE("a single line without a newline is incomplete") {
    const Lines lines = split_lines("hello");
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == "hello");
    CHECK(lines.missing_final_newline);
    CHECK(lines.is_incomplete(0));
}

TEST_CASE("a trailing newline ends the last line") {
    const Lines lines = split_lines("a\nb\n");
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "a");
    CHECK(lines[1] == "b");
    CHECK_FALSE(lines.missing_final_newline);
    CHECK_FALSE(lines.is_incomplete(1));

    const Lines partial = split_lines("a\nb");
    REQUIRE(partial.size() == 2);
    CHECK(partial[1] == "b");
    CHECK_FALSE(partial.is_incomplete(0));
    CHECK(partial.is_incomplete(1));
}

TEST_CASE("empty lines are lines") {
    const Lines one = split_lines("\n");
    REQUIRE(one.size() == 1);
    CHECK(one[0].empty());
    CHECK_FALSE(one.missing_final_newline);

    const Lines three = split_lines("\n\nx");
    REQUIRE(three.size() == 3);
    CHECK(three[0].empty());
    CHECK(three[1].empty());
    CHECK(three[2] == "x");
}

TEST_CASE("a carriage return before the newline is content") {
    const Lines lines = split_lines("one\r\ntwo\r\n\r");
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "one\r");
    CHECK(lines[1] == "two\r");
    CHECK(lines[2] == "\r");
    CHECK(lines.missing_final_newline);
}

TEST_CASE("NUL bytes are ordinary content") {
    const std::string data("a\0b\n\0\n", 6);
    const Lines lines = split_lines(data);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == std::string_view("a\0b", 3));
    CHECK(lines[1] == std::string_view("\0", 1));
}

TEST_CASE("lines are views into the text") {
    const std::string data = "first\nsecond\n";
    const Lines lines = split_lines(data);
    CHECK(lines[0].data() == data.data());
    CHECK(lines[1].data() == data.data() + 6);
}

TEST_CASE("the interner gives equal lines equal ids") {
    Interner interner;
    const std::string data = "x\ny\nx\nz\ny\n";
    const std::vector<TokenId> ids = tokenize(split_lines(data), interner);
    CHECK(ids == std::vector<TokenId>{0, 1, 0, 2, 1});
    CHECK(interner.size() == 3);
    CHECK(interner.text(2) == "z");
}

TEST_CASE("a line without its newline is a different token") {
    Interner interner;
    const TokenId complete = interner.intern("end");
    const TokenId incomplete = interner.intern("end", true);
    CHECK(complete != incomplete);
    CHECK(interner.intern("end", true) == incomplete);
    CHECK(interner.incomplete(incomplete));
    CHECK_FALSE(interner.incomplete(complete));

    const std::string a = "x\nend\n";
    const std::string b = "x\nend";
    const std::vector<TokenId> ta = tokenize(split_lines(a), interner);
    const std::vector<TokenId> tb = tokenize(split_lines(b), interner);
    CHECK(ta[1] == complete);
    CHECK(tb[1] == incomplete);
}

namespace {
std::uint64_t constant_hash(std::string_view) noexcept { return 42; }
}  // namespace

TEST_CASE("the interner compares texts in full when hashes collide") {
    Interner interner(constant_hash);
    std::vector<std::string> texts;
    for (int i = 0; i < 300; ++i) texts.push_back("line " + std::to_string(i));
    std::vector<TokenId> ids;
    for (const std::string& t : texts) ids.push_back(interner.intern(t));
    for (std::size_t i = 0; i < texts.size(); ++i) {
        CHECK(ids[i] == static_cast<TokenId>(i));
        CHECK(interner.intern(texts[i]) == ids[i]);
        CHECK(interner.text(ids[i]) == texts[i]);
    }
    CHECK(interner.intern("line 7", true) == 300);
    CHECK(interner.size() == 301);

    // Diffs come out the same with a useless hash.
    const std::string old_text = lines_text({"a", "b", "c", "d", "e"});
    const std::string new_text = lines_text({"a", "c", "d", "x", "e"});
    Interner colliding(constant_hash);
    const std::vector<TokenId> a = tokenize(split_lines(old_text), colliding);
    const std::vector<TokenId> b = tokenize(split_lines(new_text), colliding);
    CHECK(diff_tokens(a, b) == diff(split_lines(old_text), split_lines(new_text)));
}

TEST_CASE("fnv1a_hash matches published test vectors") {
    CHECK(fnv1a_hash("") == 0xCBF29CE484222325ULL);
    CHECK(fnv1a_hash("a") == 0xAF63DC4C8601EC8CULL);
    CHECK(fnv1a_hash("foobar") == 0x85944171F73967E8ULL);
}

TEST_CASE("the version matches the build") { CHECK(std::string(kVersion) == DIFFCORE_PROJECT_VERSION); }
