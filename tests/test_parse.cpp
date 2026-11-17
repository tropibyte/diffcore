// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "helpers.hpp"

using namespace diffcore;
using namespace diffcore::test;

namespace {

// The ParseError parse_patch throws for `patch_text`, if any.
std::optional<ParseError> parse_error(const std::string& patch_text) {
    try {
        parse_patch(patch_text);
    } catch (const ParseError& e) {
        return e;
    }
    return std::nullopt;
}

}  // namespace

TEST_CASE("parsing a single-file patch") {
    const std::string text =
        "--- a/config.ini\t2026-11-17 09:00:00.000000000 +0000\n"
        "+++ b/config.ini\t2026-11-17 10:00:00.000000000 +0000\n"
        "@@ -2,3 +2,4 @@ [server]\n"
        " host\n"
        "-port\n"
        "+port = 80\n"
        "+tls\n"
        " end\n"
        "@@ -10,0 +12 @@\n"
        "+appended\n";
    const Patch patch = parse_patch(text);
    REQUIRE(patch.files.size() == 1);
    const FilePatch& f = patch.files[0];
    CHECK(f.old_label == "a/config.ini\t2026-11-17 09:00:00.000000000 +0000");
    CHECK(label_path(f.new_label) == "b/config.ini");
    REQUIRE(f.hunks.size() == 2);
    const PatchHunk& h = f.hunks[0];
    CHECK(h.old_start == 1);
    CHECK(h.new_start == 1);
    CHECK(h.old_count() == 3);
    CHECK(h.new_count() == 4);
    CHECK(h.section == "[server]");
    REQUIRE(h.lines.size() == 5);
    CHECK(h.lines[1].kind == LineKind::Removed);
    CHECK(h.lines[1].text == "port");
    CHECK(h.lines[3].text == "tls");
    // An empty range names the line before it.
    CHECK(f.hunks[1].old_start == 10);
    CHECK(f.hunks[1].old_count() == 0);
    CHECK(f.hunks[1].new_start == 11);
    CHECK(format_unified(f) == text);
}

TEST_CASE("git headers and other text between files are skipped") {
    const std::string text =
        "From 1234 Mon Sep 17 00:00:00 2001\n"
        "Subject: [PATCH] two files\n"
        "---\n"
        " a.txt | 2 +-\n"
        "diff --git a/a.txt b/a.txt\n"
        "index 1111111..2222222 100644\n"
        "--- a/a.txt\n"
        "+++ b/a.txt\n"
        "@@ -1 +1 @@\n"
        "-old\n"
        "+new\n"
        "diff --git a/b.txt b/b.txt\n"
        "new file mode 100644\n"
        "--- /dev/null\n"
        "+++ b/b.txt\n"
        "@@ -0,0 +1,2 @@\n"
        "+one\n"
        "+two\n"
        "-- \n"
        "2.47.0\n";
    const Patch patch = parse_patch(text);
    REQUIRE(patch.files.size() == 2);
    CHECK(patch.files[0].old_label == "a/a.txt");
    CHECK(patch.files[1].old_label == "/dev/null");
    CHECK(patch.files[1].hunks[0].new_count() == 2);
}

TEST_CASE("text without file sections is an empty patch") {
    CHECK(parse_patch("").files.empty());
    CHECK(parse_patch("just some words\n+++ not a header\n").files.empty());
}

TEST_CASE("an empty line inside a hunk is an empty context line") {
    const std::string text = "--- a\n+++ b\n@@ -1,3 +1,3 @@\n x\n\n-y\n+z\n";
    const Patch patch = parse_patch(text);
    const PatchHunk& h = patch.files.at(0).hunks.at(0);
    REQUIRE(h.lines.size() == 4);
    CHECK(h.lines[1].kind == LineKind::Context);
    CHECK(h.lines[1].text.empty());
}

TEST_CASE("no-newline markers set the flag on the line before them") {
    const std::string text =
        "--- a\n+++ b\n@@ -1,2 +1,2 @@\n a\n-b\n\\ No newline at end of file\n+c\n\\ No newline at end of file\n";
    const Patch patch = parse_patch(text);
    const PatchHunk& h = patch.files.at(0).hunks.at(0);
    REQUIRE(h.lines.size() == 3);
    CHECK_FALSE(h.lines[0].no_newline);
    CHECK(h.lines[1].no_newline);
    CHECK(h.lines[2].no_newline);

    // Any text after the backslash is accepted (some diffs translate it).
    const std::string translated = "--- a\n+++ b\n@@ -1 +1 @@\n-x\n\\ Kein Zeilenumbruch am Dateiende.\n+y\n";
    CHECK(parse_patch(translated).files.at(0).hunks.at(0).lines.at(0).no_newline);
}

TEST_CASE("a patch file without a final newline still parses") {
    const Patch patch = parse_patch("--- a\n+++ b\n@@ -1 +1 @@\n-x\n+y");
    CHECK(patch.files.at(0).hunks.at(0).lines.at(1).text == "y");
}

TEST_CASE("formatting and parsing are inverse") {
    for (std::uint64_t seed = 1; seed <= 25; ++seed) {
        CAPTURE(seed);
        const docgen::DocPair docs = random_docs(seed, 60, 0.15, seed % 2 == 0 ? 4 : 0);
        DiffOptions options;
        options.context = static_cast<std::size_t>(seed % 4);
        const FilePatch made = make_patch(docs.old_text, docs.new_text, "old", "new", options);
        const std::string text = format_unified(made);
        const Patch parsed = parse_patch(text);
        if (made.hunks.empty()) {
            CHECK(parsed.files.empty());
            continue;
        }
        REQUIRE(parsed.files.size() == 1);
        CHECK(format_unified(parsed) == text);
        const FilePatch& f = parsed.files[0];
        REQUIRE(f.hunks.size() == made.hunks.size());
        for (std::size_t i = 0; i < f.hunks.size(); ++i) {
            CHECK(f.hunks[i].old_start == made.hunks[i].old_start);
            CHECK(f.hunks[i].new_start == made.hunks[i].new_start);
            CHECK(f.hunks[i].lines.size() == made.hunks[i].lines.size());
        }
    }
}

TEST_CASE("errors name the line of the patch") {
    struct Case {
        const char* text;
        std::size_t line;
        const char* message;
    };
    const Case cases[] = {
        {"intro\n--- a\nhello\n", 2, "\"--- \" line is not followed by a \"+++ \" line"},
        {"--- a\n", 1, "\"--- \" line is not followed by a \"+++ \" line"},
        {"--- a\n+++ b\nhello\n", 3, "expected a hunk header (\"@@ -l,s +l,s @@\")"},
        {"--- a\n+++ b\n", 2, "expected a hunk header (\"@@ -l,s +l,s @@\")"},
        {"text\n@@ -1 +1 @@\n-a\n+b\n", 2,
         "hunk header outside a file section (no \"--- \" and \"+++ \" lines before it)"},
        {"--- a\n+++ b\n@@ -x +1 @@\n", 3, "malformed hunk header: expected a number"},
        {"--- a\n+++ b\n@@ -1 +1\n", 3, "malformed hunk header: expected \"@@ -l,s +l,s @@\""},
        {"--- a\n+++ b\n@@ -1 +1 @@x\n", 3, "malformed hunk header: expected \"@@ -l,s +l,s @@\""},
        {"--- a\n+++ b\n@@ -1,1 1 @@\n", 3, "malformed hunk header: expected \"@@ -l,s +l,s @@\""},
        {"--- a\n+++ b\n@@ -0,2 +1,2 @@\n", 3,
         "malformed hunk header: line numbers of a non-empty range start at 1"},
        {"--- a\n+++ b\n@@ -99999999999999999999999 +1 @@\n", 3, "malformed hunk header: number too large"},
        {"--- a\n+++ b\n@@ -1,2 +1 @@\n+c\n+d\n-a\n-b\n", 5, "more new lines than the hunk header's 1 line"},
        {"--- a\n+++ b\n@@ -1 +1,2 @@\n-a\n-b\n", 5, "more old lines than the hunk header's 1 line"},
        {"--- a\n+++ b\n@@ -1,2 +1,2 @@\n x\n", 3,
         "the patch ends inside this hunk: expected 1 more old line and 1 more new line"},
        {"--- a\n+++ b\n@@ -1,2 +1,2 @@\n x\nzzz\n", 5,
         "unexpected line in hunk; expected 1 more old line and 1 more new line"},
        {"--- a\n+++ b\n@@ -1,3 +1,3 @@\n x\n-a\n+b\ndiff --git a/c b/c\n", 7,
         "unexpected line in hunk; expected 1 more old line and 1 more new line"},
        {"--- a\n+++ b\n@@ -1 +1 @@\n\\ No newline at end of file\n-a\n+b\n", 4,
         "\"\\ No newline at end of file\" with no line before it"},
        {"--- a\n+++ b\n@@ -1,2 +1 @@\n-a\n\\ No newline at end of file\n-b\n+c\n", 5,
         "\"\\ No newline at end of file\" after a line that is not the last of the hunk's old or new text"},
        {"--- a\n+++ b\n@@ -1 +1,2 @@\n x\n\\ No newline at end of file\n+y\n", 5,
         "\"\\ No newline at end of file\" after a line that is not the last of the hunk's old or new text"},
        {"--- a\n+++ b\n@@ -1 +1 @@\n-a\n\\ x\n\\ y\n+b\n", 6, "repeated \"\\ No newline at end of file\""},
        {"--- a\n+++ b\n@@ -5 +5 @@\n-a\n+b\n@@ -3 +3 @@\n-c\n+d\n", 6,
         "hunk overlaps or comes before the previous hunk"},
        {"--- a\n+++ b\n@@ -1,2 +1,2 @@\n-a\n-b\n+c\n+d\n@@ -2 +3 @@\n-e\n+f\n", 8,
         "hunk overlaps or comes before the previous hunk"},
    };
    for (const Case& c : cases) {
        CAPTURE(c.text);
        const std::optional<ParseError> e = parse_error(c.text);
        REQUIRE(e.has_value());
        CHECK(e->line() == c.line);
        CHECK(e->message() == c.message);
        CHECK(std::string(e->what()) == "line " + std::to_string(c.line) + ": " + c.message);
    }
}

TEST_CASE("parse errors are diffcore errors") {
    CHECK_THROWS_AS(parse_patch("--- a\n+++ b\n@@ bad\n"), Error);
}
