// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.
//
// Shared test helpers.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "diffcore/diffcore.hpp"
#include "docgen.hpp"
#include "doctest.h"

namespace diffcore {

// Lets doctest print edits in failure messages.
inline std::ostream& operator<<(std::ostream& os, const Edit& e) {
    const char* kind = e.kind == EditKind::Equal ? "Equal" : e.kind == EditKind::Insert ? "Insert" : "Delete";
    return os << kind << '(' << e.old_start << ',' << e.new_start << ',' << e.count << ')';
}

}  // namespace diffcore

namespace diffcore::test {

inline const std::filesystem::path& data_dir() {
    static const std::filesystem::path dir(DIFFCORE_TEST_DATA_DIR);
    return dir;
}

inline std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "cannot open " << path.string());
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

/// Lines joined with '\n' after each.
inline std::string lines_text(std::initializer_list<std::string_view> lines) {
    std::string out;
    for (const std::string_view line : lines) {
        out += line;
        out += '\n';
    }
    return out;
}

/// "1\n2\n...\nn\n".
inline std::string numbered(std::size_t n) {
    std::string out;
    for (std::size_t i = 1; i <= n; ++i) out += std::to_string(i) + '\n';
    return out;
}

/// unified_diff with the labels a and b.
inline std::string udiff(std::string_view old_text, std::string_view new_text, std::size_t context = 3) {
    DiffOptions options;
    options.context = context;
    return unified_diff(old_text, new_text, "a", "b", options);
}

/// Token sequence from a string of single-character tokens: "abca".
inline std::vector<TokenId> tokens(std::string_view s) {
    std::vector<TokenId> out;
    for (const char c : s) out.push_back(static_cast<TokenId>(static_cast<unsigned char>(c)));
    return out;
}

/// Applies an edit script to `a`, checking it is well formed and consistent
/// with `a` and `b` along the way. Returns the reconstructed b.
inline std::vector<TokenId> replay(const EditScript& script, const std::vector<TokenId>& a,
                                   const std::vector<TokenId>& b) {
    std::vector<TokenId> out;
    std::size_t i = 0;
    std::size_t j = 0;
    for (std::size_t e = 0; e < script.size(); ++e) {
        const Edit& edit = script[e];
        CHECK(edit.count > 0);
        CHECK(edit.old_start == i);
        CHECK(edit.new_start == j);
        if (e > 0) {
            CHECK(edit.kind != script[e - 1].kind);
            CHECK_FALSE((edit.kind == EditKind::Delete && script[e - 1].kind == EditKind::Insert));
        }
        for (std::size_t t = 0; t < edit.count; ++t) {
            switch (edit.kind) {
                case EditKind::Equal:
                    REQUIRE(i < a.size());
                    REQUIRE(j < b.size());
                    CHECK(a[i] == b[j]);
                    out.push_back(a[i]);
                    ++i;
                    ++j;
                    break;
                case EditKind::Delete:
                    REQUIRE(i < a.size());
                    ++i;
                    break;
                case EditKind::Insert:
                    REQUIRE(j < b.size());
                    out.push_back(b[j]);
                    ++j;
                    break;
            }
        }
    }
    CHECK(i == a.size());
    CHECK(j == b.size());
    return out;
}

/// Length of a longest common subsequence, by dynamic programming.
inline std::size_t lcs_length(const std::vector<TokenId>& a, const std::vector<TokenId>& b) {
    std::vector<std::size_t> row(b.size() + 1, 0);
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t diagonal = 0;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t above = row[j];
            row[j] = a[i - 1] == b[j - 1] ? diagonal + 1 : (row[j] > row[j - 1] ? row[j] : row[j - 1]);
            diagonal = above;
        }
    }
    return row[b.size()];
}

/// A pair of random documents with repeated lines and, depending on the
/// seed, a missing final newline on either side.
inline docgen::DocPair random_docs(std::uint64_t seed, std::size_t lines, double rate, std::size_t vocabulary) {
    docgen::Params params;
    params.lines = lines;
    params.change_rate = rate;
    params.vocabulary = vocabulary;
    docgen::DocPair docs = docgen::generate(params, seed);
    if (seed % 3 == 1 && !docs.old_text.empty()) docs.old_text.pop_back();
    if (seed % 4 == 2 && !docs.new_text.empty()) docs.new_text.pop_back();
    return docs;
}

}  // namespace diffcore::test
