// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.
//
// Library walk-through: diff two texts, print the unified diff, parse it
// back, apply it to the old text, then apply it to a copy of the old text
// that has gained a line at the top, to show hunks found at an offset.
//
//   diffcore_example OLD NEW

#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include "diffcore/diffcore.hpp"

namespace {

bool read_file(const char* path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !in.bad();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: diffcore_example OLD NEW\n";
        return 2;
    }
    std::string old_text;
    std::string new_text;
    if (!read_file(argv[1], old_text) || !read_file(argv[2], new_text)) {
        std::cerr << "cannot read the input files\n";
        return 2;
    }

    try {
        // 1. The diff, as `diff -u` would print it.
        diffcore::DiffOptions options;
        options.context = 2;
        const std::string patch_text = diffcore::unified_diff(old_text, new_text, argv[1], argv[2], options);
        if (patch_text.empty()) {
            std::cout << "the files are the same\n";
            return 0;
        }
        std::cout << patch_text;

        // 2. Parse it and apply it to the old text.
        const diffcore::Patch patch = diffcore::parse_patch(patch_text);
        const diffcore::FilePatch& file = patch.files.at(0);
        const diffcore::ApplyResult exact = diffcore::apply_patch(old_text, file);
        if (!exact.ok() || exact.output != new_text) {
            std::cerr << "applying the diff did not give the new text\n";
            return 1;
        }
        std::cout << "\napplied " << file.hunks.size() << " hunk(s): the result equals " << argv[2] << '\n';

        // 3. The same patch on a text with an extra first line: every hunk
        //    is found one line further down.
        const std::string shifted = "an extra first line\n" + old_text;
        const diffcore::ApplyResult moved = diffcore::apply_patch(shifted, file);
        for (std::size_t i = 0; i < moved.hunks.size(); ++i) {
            const diffcore::HunkResult& h = moved.hunks[i];
            std::cout << "hunk " << i + 1 << (h.applied ? " applied at line " : " failed at line ") << h.line
                      << " (offset " << h.offset << ")\n";
        }

        // 4. And in reverse, back to the old text.
        diffcore::ApplyOptions reverse;
        reverse.reverse = true;
        const diffcore::ApplyResult back = diffcore::apply_patch(new_text, file, reverse);
        std::cout << "reverse apply " << (back.ok() && back.output == old_text ? "restores" : "does not restore")
                  << " the old text\n";
        return 0;
    } catch (const diffcore::Error& e) {
        std::cerr << e.what() << '\n';
        return 2;
    }
}
