// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.

#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "diffcore/error.hpp"
#include "diffcore/lines.hpp"
#include "diffcore/patch.hpp"
#include "diffcore/unified.hpp"

namespace diffcore {

namespace {

constexpr std::string_view kMarkerName = "\"\\ No newline at end of file\"";

bool starts_with(std::string_view s, std::string_view prefix) noexcept {
    return s.substr(0, prefix.size()) == prefix;
}

bool consume(std::string_view& s, std::string_view prefix) noexcept {
    if (!starts_with(s, prefix)) return false;
    s.remove_prefix(prefix.size());
    return true;
}

std::string plural(std::size_t n, const char* word) {
    std::string s = std::to_string(n) + ' ' + word;
    if (n != 1) s += 's';
    return s;
}

class Parser {
public:
    explicit Parser(std::string_view text) : lines_(split_lines(text).lines) {}

    Patch parse() {
        Patch patch;
        while (pos_ < lines_.size()) {
            const std::string_view line = lines_[pos_];
            if (starts_with(line, "--- ")) {
                patch.files.push_back(parse_file());
            } else if (starts_with(line, "@@ -")) {
                fail(pos_ + 1, "hunk header outside a file section (no \"--- \" and \"+++ \" lines before it)");
            } else {
                ++pos_;
            }
        }
        return patch;
    }

private:
    [[noreturn]] static void fail(std::size_t line, const std::string& message) { throw ParseError(line, message); }

    FilePatch parse_file() {
        FilePatch file;
        if (pos_ + 1 >= lines_.size() || !starts_with(lines_[pos_ + 1], "+++ ")) {
            fail(pos_ + 1, "\"--- \" line is not followed by a \"+++ \" line");
        }
        file.old_label = std::string(lines_[pos_].substr(4));
        file.new_label = std::string(lines_[pos_ + 1].substr(4));
        pos_ += 2;
        while (pos_ < lines_.size() && starts_with(lines_[pos_], "@@")) {
            file.hunks.push_back(parse_hunk(file.hunks.empty() ? nullptr : &file.hunks.back()));
        }
        if (file.hunks.empty()) {
            fail(pos_ < lines_.size() ? pos_ + 1 : pos_, "expected a hunk header (\"@@ -l,s +l,s @@\")");
        }
        return file;
    }

    // One side of a hunk header: "l" or "l,s". Returns the 0-based start.
    static std::size_t parse_range(std::string_view& s, std::size_t line, std::size_t& count) {
        std::size_t first = 0;
        read_number(s, line, first);
        count = 1;
        if (consume(s, ",")) read_number(s, line, count);
        if (count == 0) return first;
        if (first == 0) fail(line, "malformed hunk header: line numbers of a non-empty range start at 1");
        return first - 1;
    }

    static void read_number(std::string_view& s, std::size_t line, std::size_t& value) {
        const char* begin = s.data();
        const char* end = s.data() + s.size();
        const auto [ptr, ec] = std::from_chars(begin, end, value);
        if (ec == std::errc::result_out_of_range || (ec == std::errc{} && value > kMaxLineNumber)) {
            fail(line, "malformed hunk header: number too large");
        }
        if (ec != std::errc{} || ptr == begin) fail(line, "malformed hunk header: expected a number");
        s.remove_prefix(static_cast<std::size_t>(ptr - begin));
    }

    PatchHunk parse_header(std::size_t line, std::size_t& old_count, std::size_t& new_count) const {
        static const std::string kExpected = "malformed hunk header: expected \"@@ -l,s +l,s @@\"";
        PatchHunk hunk;
        std::string_view s = lines_[line - 1];
        if (!consume(s, "@@ -")) fail(line, kExpected);
        hunk.old_start = parse_range(s, line, old_count);
        if (!consume(s, " +")) fail(line, kExpected);
        hunk.new_start = parse_range(s, line, new_count);
        if (!consume(s, " @@")) fail(line, kExpected);
        if (!s.empty()) {
            if (s.front() != ' ') fail(line, kExpected);
            hunk.section = std::string(s.substr(1));
        }
        return hunk;
    }

    PatchHunk parse_hunk(const PatchHunk* previous) {
        const std::size_t header_line = pos_ + 1;
        std::size_t need_old = 0;
        std::size_t need_new = 0;
        PatchHunk hunk = parse_header(header_line, need_old, need_new);
        const std::size_t old_count = need_old;
        const std::size_t new_count = need_new;
        ++pos_;

        if (previous != nullptr && (hunk.old_start < previous->old_start + previous->old_count() ||
                                    hunk.new_start < previous->new_start + previous->new_count())) {
            fail(header_line, "hunk overlaps or comes before the previous hunk");
        }

        std::vector<std::size_t> marker_lines;  // per hunk line: line of its marker, or 0
        auto mark = [&](std::size_t line) {
            if (hunk.lines.empty()) fail(line, std::string(kMarkerName) + " with no line before it");
            if (hunk.lines.back().no_newline) fail(line, "repeated " + std::string(kMarkerName));
            hunk.lines.back().no_newline = true;
            marker_lines.back() = line;
        };
        auto add = [&](LineKind kind, std::string_view text) {
            hunk.lines.push_back(PatchLine{kind, std::string(text), false});
            marker_lines.push_back(0);
        };
        auto remaining = [&] {
            return "expected " + plural(need_old, "more old line") + " and " + plural(need_new, "more new line");
        };

        while (need_old > 0 || need_new > 0) {
            if (pos_ >= lines_.size()) fail(header_line, "the patch ends inside this hunk: " + remaining());
            const std::string_view line = lines_[pos_];
            const std::size_t number = pos_ + 1;
            const char tag = line.empty() ? ' ' : line.front();
            const std::string_view text = line.empty() ? line : line.substr(1);
            switch (tag) {
                case ' ':
                    if (need_old == 0) fail(number, "more old lines than the hunk header's " + plural(old_count, "line"));
                    if (need_new == 0) fail(number, "more new lines than the hunk header's " + plural(new_count, "line"));
                    --need_old;
                    --need_new;
                    add(LineKind::Context, text);
                    break;
                case '-':
                    if (need_old == 0) fail(number, "more old lines than the hunk header's " + plural(old_count, "line"));
                    --need_old;
                    add(LineKind::Removed, text);
                    break;
                case '+':
                    if (need_new == 0) fail(number, "more new lines than the hunk header's " + plural(new_count, "line"));
                    --need_new;
                    add(LineKind::Added, text);
                    break;
                case '\\':
                    mark(number);
                    break;
                default:
                    fail(number, "unexpected line in hunk; " + remaining());
            }
            ++pos_;
        }
        if (pos_ < lines_.size() && starts_with(lines_[pos_], "\\")) {
            mark(pos_ + 1);
            ++pos_;
        }
        check_markers(hunk, marker_lines, old_count, new_count);
        return hunk;
    }

    // The marker may only follow the last line of the old text, of the new
    // text, or of both (a context line).
    static void check_markers(const PatchHunk& hunk, const std::vector<std::size_t>& marker_lines,
                              std::size_t old_count, std::size_t new_count) {
        std::size_t old_index = 0;
        std::size_t new_index = 0;
        for (std::size_t i = 0; i < hunk.lines.size(); ++i) {
            const PatchLine& line = hunk.lines[i];
            const bool in_old = line.kind != LineKind::Added;
            const bool in_new = line.kind != LineKind::Removed;
            if (line.no_newline) {
                const bool last_old = old_index + 1 == old_count;
                const bool last_new = new_index + 1 == new_count;
                if ((in_old && !last_old) || (in_new && !last_new)) {
                    fail(marker_lines[i], std::string(kMarkerName) +
                                              " after a line that is not the last of the hunk's old or new text");
                }
            }
            if (in_old) ++old_index;
            if (in_new) ++new_index;
        }
    }

    std::vector<std::string_view> lines_;
    std::size_t pos_ = 0;  // index of the next line; its line number is pos_ + 1
};

}  // namespace

Patch parse_patch(std::string_view text) { return Parser(text).parse(); }

}  // namespace diffcore
