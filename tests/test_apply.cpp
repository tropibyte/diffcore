// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

#include "helpers.hpp"

using namespace diffcore;
using namespace diffcore::test;

namespace {

// Replaces the line `line` (without its newline) of `text` with `with`,
// which may span several lines.
std::string replace_line(std::string text, std::string_view line, std::string_view with) {
    const std::string needle = "\n" + std::string(line) + "\n";
    const std::string padded = "\n" + text;
    const std::size_t at = padded.find(needle);
    REQUIRE_MESSAGE(at != std::string::npos, "no line " << line);
    text.replace(at, line.size(), with);
    return text;
}

FilePatch patch_of(const std::string& a, const std::string& b, std::size_t context = 3) {
    DiffOptions options;
    options.context = context;
    // Through the text form, as a patch file would arrive.
    const Patch parsed = parse_patch(format_unified(make_patch(a, b, "a", "b", options)));
    REQUIRE(parsed.files.size() == 1);
    return parsed.files[0];
}

// The two-hunk patch used in several tests: line 5 gains two lines after
// it, line 30 is replaced.
FilePatch two_hunks() {
    const std::string a = numbered(40);
    const std::string b = replace_line(replace_line(a, "5", "5\nA\nB"), "30", "thirty");
    return patch_of(a, b);
}

}  // namespace

TEST_CASE("a patch applies at its stated lines") {
    const std::string a = numbered(40);
    const std::string b = replace_line(replace_line(a, "5", "5\nA\nB"), "30", "thirty");
    const ApplyResult r = apply_patch(a, two_hunks());
    REQUIRE(r.ok());
    CHECK(r.output == b);
    REQUIRE(r.hunks.size() == 2);
    CHECK(r.hunks[0].applied);
    CHECK(r.hunks[0].offset == 0);
    CHECK(r.hunks[0].line == 3);
    CHECK(r.hunks[1].offset == 0);
    CHECK(r.hunks[1].line == 29);
}

TEST_CASE("hunks are found below their stated lines") {
    const std::string a = "x\n" + numbered(40);
    const ApplyResult r = apply_patch(a, two_hunks());
    REQUIRE(r.ok());
    CHECK(r.output == "x\n" + replace_line(replace_line(numbered(40), "5", "5\nA\nB"), "30", "thirty"));
    CHECK(r.hunks[0].offset == 1);
    CHECK(r.hunks[0].line == 4);
    CHECK(r.hunks[1].offset == 1);
    CHECK(r.hunks[1].line == 30);
}

TEST_CASE("hunks are found above their stated lines") {
    std::string a = numbered(40);
    a.erase(0, a.find('\n') + 1);  // lines 2..40
    const ApplyResult r = apply_patch(a, two_hunks());
    REQUIRE(r.ok());
    CHECK(r.hunks[0].offset == -1);
    CHECK(r.hunks[0].line == 2);
    CHECK(r.hunks[1].offset == -1);
    CHECK(r.hunks[1].line == 28);
}

TEST_CASE("each hunk's search starts from the previous hunk's offset") {
    // Two lines more between the hunks: the second hunk sits two lines
    // further down than the first one's offset (0) suggests.
    const std::string a = replace_line(numbered(40), "20", "20\nq\nq");
    const ApplyResult r = apply_patch(a, two_hunks());
    REQUIRE(r.ok());
    CHECK(r.hunks[0].offset == 0);
    CHECK(r.hunks[1].offset == 2);
    CHECK(r.hunks[1].line == 31);
}

TEST_CASE("a failed hunk leaves the text unchanged") {
    const std::string a = replace_line(numbered(40), "29", "twentynine");
    const ApplyResult r = apply_patch(a, two_hunks());
    CHECK_FALSE(r.ok());
    CHECK(r.failed_count() == 1);
    CHECK(r.output.empty());
    CHECK(r.hunks[0].applied);
    CHECK_FALSE(r.hunks[1].applied);
    // Reported where it would have gone: line 27, moved by the first hunk's
    // two added lines.
    CHECK(r.hunks[1].line == 29);
}

TEST_CASE("hunks after a failed one still apply, from the last good offset") {
    const std::string a = "x\n" + replace_line(numbered(40), "4", "four");
    const ApplyResult r = apply_patch(a, two_hunks());
    CHECK(r.failed_count() == 1);
    CHECK_FALSE(r.hunks[0].applied);
    CHECK(r.hunks[0].line == 3);
    CHECK(r.hunks[1].applied);
    CHECK(r.hunks[1].offset == 1);
    CHECK(r.hunks[1].line == 28);
    CHECK(r.output.empty());
}

TEST_CASE("a dry run reports without producing output") {
    ApplyOptions options;
    options.dry_run = true;
    const ApplyResult r = apply_patch("x\n" + numbered(40), two_hunks(), options);
    CHECK(r.ok());
    CHECK(r.output.empty());
    CHECK(r.hunks[1].offset == 1);
}

TEST_CASE("reverse apply undoes the patch") {
    const std::string a = numbered(40);
    const std::string b = replace_line(replace_line(a, "5", "5\nA\nB"), "30", "thirty");
    ApplyOptions options;
    options.reverse = true;
    const ApplyResult r = apply_patch(b, two_hunks(), options);
    REQUIRE(r.ok());
    CHECK(r.output == a);
    // Applying it forwards to the new text fails.
    CHECK_FALSE(apply_patch(b, two_hunks()).ok());
}

TEST_CASE("a hunk with less trailing than leading context only applies at the end") {
    const std::string a = numbered(10);
    const FilePatch p = patch_of(a, replace_line(a, "10", "TEN"));
    REQUIRE(p.hunks.size() == 1);
    CHECK(apply_patch(a, p).ok());
    // Lines added after it: an exact match remains, but not at the end.
    CHECK_FALSE(apply_patch(a + "11\n12\n", p).ok());
    // Lines added before it: found at the end.
    const ApplyResult r = apply_patch(a + a, p);
    REQUIRE(r.ok());
    CHECK(r.hunks[0].offset == 10);
    CHECK(r.hunks[0].line == 17);
    CHECK(r.output == a + replace_line(a, "10", "TEN"));
}

TEST_CASE("a hunk with less leading than trailing context stated at line 1 only applies at the start") {
    const std::string a = numbered(10);
    const FilePatch p = patch_of(a, replace_line(a, "1", "ONE"));
    CHECK(apply_patch(a, p).ok());
    CHECK_FALSE(apply_patch("0\n" + a, p).ok());
    const ApplyResult r = apply_patch(a + a, p);
    REQUIRE(r.ok());
    CHECK(r.hunks[0].line == 1);
    CHECK(r.output == replace_line(a, "1", "ONE") + a);
}

TEST_CASE("the newline at the end of the file must match") {
    const FilePatch p = patch_of("a\nb", "a\nc");
    CHECK(apply_patch("a\nb", p).output == "a\nc");
    CHECK_FALSE(apply_patch("a\nb\n", p).ok());

    const FilePatch add = patch_of("a\nb", "a\nb\n");
    CHECK(apply_patch("a\nb", add).output == "a\nb\n");
    ApplyOptions reverse;
    reverse.reverse = true;
    CHECK(apply_patch("a\nb\n", add, reverse).output == "a\nb");
}

TEST_CASE("a hunk whose new text has no final newline must reach the end") {
    // "x" loses its newline, so nothing may follow it.
    const FilePatch p = patch_of("x\n", "x");
    CHECK(apply_patch("x\n", p).output == "x");
    CHECK_FALSE(apply_patch("x\ny\n", p).ok());
}

TEST_CASE("pure insertions apply at their shifted position without a search") {
    const std::string a = lines_text({"a", "b", "c"});
    const FilePatch p = patch_of(a, lines_text({"a", "X", "b", "c", "Y"}), 0);
    REQUIRE(p.hunks.size() == 2);
    CHECK(apply_patch(a, p).output == lines_text({"a", "X", "b", "c", "Y"}));
    // Appending after a last line that lacks its newline would join lines.
    const FilePatch append = patch_of("a\n", "a\nb\n", 0);
    CHECK(apply_patch("a\n", append).output == "a\nb\n");
    CHECK_FALSE(apply_patch("a", append).ok());
    // Positions past the end fail.
    CHECK_FALSE(apply_patch("", append).ok());
}

TEST_CASE("hunks never match lines an earlier hunk consumed") {
    // Both hunks change an "x" between "a" and "b"; in a text with one such
    // place, only the first can apply.
    FilePatch p;
    for (const std::size_t start : {std::size_t{0}, std::size_t{3}}) {
        PatchHunk h;
        h.old_start = start;
        h.new_start = start;
        h.lines = {{LineKind::Context, "a", false},
                   {LineKind::Removed, "x", false},
                   {LineKind::Added, "y", false},
                   {LineKind::Context, "b", false}};
        p.hunks.push_back(h);
    }
    const ApplyResult r = apply_patch(lines_text({"a", "x", "b"}), p);
    CHECK(r.hunks[0].applied);
    CHECK_FALSE(r.hunks[1].applied);
    CHECK(apply_patch(lines_text({"a", "x", "b", "a", "x", "b"}), p).output == lines_text({"a", "y", "b", "a", "y", "b"}));
}

TEST_CASE("odd hand-built patches fail cleanly") {
    FilePatch p;
    PatchHunk far;
    far.old_start = 1000000;
    far.new_start = 1000000;
    far.lines = {{LineKind::Removed, "1", false}, {LineKind::Added, "one", false}};
    p.hunks.push_back(far);
    // The search reaches line 1 from far below.
    CHECK(apply_patch(numbered(3), p).output == "one\n2\n3\n");

    PatchHunk huge = far;
    huge.old_start = kMaxLineNumber + 1;
    p.hunks = {huge};
    CHECK_FALSE(apply_patch(numbered(3), p).ok());

    PatchHunk misplaced;
    misplaced.lines = {{LineKind::Removed, "1", true}, {LineKind::Removed, "2", false}};
    p.hunks = {misplaced};
    CHECK_FALSE(apply_patch(numbered(3), p).ok());
}

TEST_CASE("a patch without hunks leaves the text as it is") {
    const ApplyResult r = apply_patch("a\nb", FilePatch{});
    CHECK(r.ok());
    CHECK(r.output == "a\nb");
}

TEST_CASE("deleting everything gives an empty text") {
    const ApplyResult r = apply_patch("a\nb", patch_of("a\nb", ""));
    CHECK(r.ok());
    CHECK(r.output.empty());
}

TEST_CASE("round trips over random documents") {
    for (std::size_t seed = 1; seed <= 60; ++seed) {
        CAPTURE(seed);
        const std::size_t vocabulary = seed % 3 == 0 ? 0 : 2 + seed % 5;
        const double rate = 0.05 + 0.01 * static_cast<double>(seed % 20);
        const docgen::DocPair docs = random_docs(seed, 20 + seed * 3, rate, vocabulary);
        const FilePatch p = patch_of(docs.old_text, docs.new_text, seed % 5);

        const ApplyResult forward = apply_patch(docs.old_text, p);
        REQUIRE(forward.ok());
        CHECK(forward.output == docs.new_text);
        for (const HunkResult& h : forward.hunks) CHECK(h.offset == 0);

        ApplyOptions options;
        options.reverse = true;
        const ApplyResult back = apply_patch(docs.new_text, p, options);
        REQUIRE(back.ok());
        CHECK(back.output == docs.old_text);
    }
}
