// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.

#include <string>

#include "helpers.hpp"

using namespace diffcore;
using namespace diffcore::test;

TEST_CASE("hunk ranges follow diff -u conventions") {
    CHECK(format_range(0, 0) == "0,0");
    CHECK(format_range(4, 0) == "4,0");
    CHECK(format_range(0, 1) == "1");
    CHECK(format_range(4, 1) == "5");
    CHECK(format_range(0, 3) == "1,3");
    CHECK(format_range(9, 12) == "10,12");
}

TEST_CASE("equal texts give no output") {
    CHECK(udiff("", "").empty());
    CHECK(udiff("a\nb\n", "a\nb\n").empty());
    CHECK(udiff("a\nb", "a\nb").empty());
}

TEST_CASE("an empty old file") {
    const std::string expected = "--- a\n+++ b\n@@ -0,0 +1,2 @@\n+a\n+b\n";
    CHECK(udiff("", "a\nb\n") == expected);
}

TEST_CASE("an empty new file") {
    const std::string expected = "--- a\n+++ b\n@@ -1,2 +0,0 @@\n-a\n-b\n";
    CHECK(udiff("a\nb\n", "") == expected);
}

TEST_CASE("an added line only") {
    const std::string expected =
        "--- a\n+++ b\n"
        "@@ -2,6 +2,7 @@\n 2\n 3\n 4\n+new\n 5\n 6\n 7\n";
    std::string changed = numbered(10);
    changed.insert(changed.find("5\n"), "new\n");
    CHECK(udiff(numbered(10), changed) == expected);
}

TEST_CASE("a deleted line only") {
    const std::string expected =
        "--- a\n+++ b\n"
        "@@ -2,7 +2,6 @@\n 2\n 3\n 4\n-5\n 6\n 7\n 8\n";
    std::string changed = numbered(10);
    changed.erase(changed.find("5\n"), 2);
    CHECK(udiff(numbered(10), changed) == expected);
}

TEST_CASE("a change at line 1 has no leading context") {
    const std::string expected = "--- a\n+++ b\n@@ -1,4 +1,4 @@\n-1\n+one\n 2\n 3\n 4\n";
    std::string changed = numbered(10);
    changed.replace(0, 1, "one");
    CHECK(udiff(numbered(10), changed) == expected);
}

TEST_CASE("a change at the last line") {
    const std::string expected = "--- a\n+++ b\n@@ -7,4 +7,4 @@\n 7\n 8\n 9\n-10\n+ten\n";
    std::string changed = numbered(10);
    changed.replace(changed.find("10\n"), 2, "ten");
    CHECK(udiff(numbered(10), changed) == expected);
}

TEST_CASE("a change at the last line when neither file ends with a newline") {
    const std::string expected =
        "--- a\n+++ b\n@@ -1,2 +1,2 @@\n a\n-b\n\\ No newline at end of file\n+c\n\\ No newline at end of file\n";
    CHECK(udiff("a\nb", "a\nc") == expected);
}

TEST_CASE("adding and removing the final newline") {
    const std::string added = "--- a\n+++ b\n@@ -1,2 +1,2 @@\n a\n-b\n\\ No newline at end of file\n+b\n";
    CHECK(udiff("a\nb", "a\nb\n") == added);
    const std::string removed = "--- a\n+++ b\n@@ -1,2 +1,2 @@\n a\n-b\n+b\n\\ No newline at end of file\n";
    CHECK(udiff("a\nb\n", "a\nb") == removed);
}

TEST_CASE("an unchanged last line without a newline in the context") {
    const std::string expected = "--- a\n+++ b\n@@ -1,3 +1,3 @@\n-x\n+y\n a\n b\n\\ No newline at end of file\n";
    CHECK(udiff("x\na\nb", "y\na\nb") == expected);
}

TEST_CASE("lines added after a last line that had no newline") {
    const std::string expected = "--- a\n+++ b\n@@ -1 +1,2 @@\n-a\n\\ No newline at end of file\n+a\n+b\n";
    CHECK(udiff("a", "a\nb\n") == expected);
    const std::string kept = "--- a\n+++ b\n@@ -1 +1,3 @@\n a\n+b\n+c\n\\ No newline at end of file\n";
    CHECK(udiff("a\n", "a\nb\nc") == kept);
}

TEST_CASE("zero context") {
    const std::string expected = "--- a\n+++ b\n@@ -1,0 +2 @@\n+X\n@@ -3,0 +5 @@\n+Y\n";
    CHECK(udiff("a\nb\nc\n", "a\nX\nb\nc\nY\n", 0) == expected);
    const std::string removed = "--- a\n+++ b\n@@ -2 +1,0 @@\n-b\n";
    CHECK(udiff("a\nb\nc\n", "a\nc\n", 0) == removed);
}

TEST_CASE("carriage returns are kept as line content") {
    const std::string expected = "--- a\n+++ b\n@@ -1,2 +1,2 @@\n x\r\n-y\r\n+z\r\n";
    CHECK(udiff("x\r\ny\r\n", "x\r\nz\r\n") == expected);
    // A line ending change is a change.
    const std::string endings = "--- a\n+++ b\n@@ -1 +1 @@\n-x\n+x\r\n";
    CHECK(udiff("x\n", "x\r\n") == endings);
}

TEST_CASE("headers carry the labels as given") {
    const std::string out = unified_diff("a\n", "b\n", "old/file.txt\t2026-11-17 09:00:00.000000000 +0000",
                                         "new/file.txt\t2026-11-17 10:00:00.000000000 +0000");
    const std::string expected =
        "--- old/file.txt\t2026-11-17 09:00:00.000000000 +0000\n"
        "+++ new/file.txt\t2026-11-17 10:00:00.000000000 +0000\n"
        "@@ -1 +1 @@\n-a\n+b\n";
    CHECK(out == expected);
    CHECK(label_path("old/file.txt\t2026-11-17") == "old/file.txt");
    CHECK(label_path("plain") == "plain");
}

TEST_CASE("hunk headers keep their section text") {
    PatchHunk hunk;
    hunk.old_start = 9;
    hunk.new_start = 9;
    hunk.lines = {{LineKind::Context, "x", false}, {LineKind::Added, "y", false}};
    hunk.section = "int main()";
    CHECK(format_hunk_header(hunk) == "@@ -10 +10,2 @@ int main()");
}

TEST_CASE("a multi-file patch is the file sections in order") {
    Patch patch;
    patch.files.push_back(make_patch("a\n", "b\n", "one", "one"));
    patch.files.push_back(make_patch("same\n", "same\n", "two", "two"));
    patch.files.push_back(make_patch("", "c\n", "three", "three"));
    const std::string expected =
        "--- one\n+++ one\n@@ -1 +1 @@\n-a\n+b\n"
        "--- three\n+++ three\n@@ -0,0 +1 @@\n+c\n";
    CHECK(format_unified(patch) == expected);
}
