// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.
//
// tests/data/golden holds file pairs NAME.old / NAME.new and NAME.diff, the
// output of `diff -u --label a --label b` (GNU diffutils 3.10), written by
// tools/make_golden.sh. Names starting with "u<N>_" were made with -U N.

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "helpers.hpp"

using namespace diffcore;
using namespace diffcore::test;

namespace {

std::vector<std::string> golden_names() {
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(data_dir() / "golden")) {
        if (entry.path().extension() == ".diff") names.push_back(entry.path().stem().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

// N from a "u<N>_" prefix; 3 without one.
std::size_t context_of(const std::string& name) {
    std::size_t context = 0;
    std::size_t i = 1;
    while (i < name.size() && name[i] >= '0' && name[i] <= '9') {
        context = context * 10 + static_cast<std::size_t>(name[i++] - '0');
    }
    const bool prefixed = name[0] == 'u' && i > 1 && i < name.size() && name[i] == '_';
    return prefixed ? context : 3;
}

}  // namespace

TEST_CASE("unified diffs match GNU diff byte for byte") {
    const std::vector<std::string> names = golden_names();
    REQUIRE(names.size() >= 15);
    for (const std::string& name : names) {
        CAPTURE(name);
        const auto base = data_dir() / "golden" / name;
        const std::string old_text = read_file(base.string() + ".old");
        const std::string new_text = read_file(base.string() + ".new");
        const std::string expected = read_file(base.string() + ".diff");
        CHECK(udiff(old_text, new_text, context_of(name)) == expected);
    }
}

TEST_CASE("GNU diff output applies, forwards and in reverse") {
    for (const std::string& name : golden_names()) {
        CAPTURE(name);
        const auto base = data_dir() / "golden" / name;
        const std::string old_text = read_file(base.string() + ".old");
        const std::string new_text = read_file(base.string() + ".new");
        const Patch patch = parse_patch(read_file(base.string() + ".diff"));
        REQUIRE(patch.files.size() == 1);
        const ApplyResult forward = apply_patch(old_text, patch.files[0]);
        REQUIRE(forward.ok());
        CHECK(forward.output == new_text);
        ApplyOptions options;
        options.reverse = true;
        const ApplyResult back = apply_patch(new_text, patch.files[0], options);
        REQUIRE(back.ok());
        CHECK(back.output == old_text);
    }
}
