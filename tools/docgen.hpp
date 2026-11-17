// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.
//
// Deterministic pseudo-random document pairs for `diffcore bench`, the
// oracle script and the tests. The same seed gives the same documents with
// every compiler and standard library: no std:: distributions are used.

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace diffcore::docgen {

/// SplitMix64.
class Rng {
public:
    explicit Rng(std::uint64_t seed) noexcept : state_(seed) {}

    std::uint64_t next() noexcept {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    /// Uniform in [0, n); n must be positive.
    std::uint64_t below(std::uint64_t n) noexcept {
        // Reject the top partial block so every residue is equally likely.
        const std::uint64_t limit = (std::numeric_limits<std::uint64_t>::max)() -
                                    (std::numeric_limits<std::uint64_t>::max)() % n;
        std::uint64_t v = next();
        while (v >= limit) v = next();
        return v % n;
    }

    std::size_t index(std::size_t n) noexcept { return static_cast<std::size_t>(below(n)); }

    /// True with probability `rate` (clamped to [0, 1]).
    bool chance(double rate) noexcept {
        if (!(rate > 0.0)) return false;
        if (rate >= 1.0) return true;
        const auto threshold = static_cast<std::uint64_t>(rate * 4294967296.0);
        return (next() >> 32) < threshold;
    }

private:
    std::uint64_t state_;
};

struct Params {
    std::size_t lines = 1000;
    /// Chance that each old line is deleted, replaced or has lines inserted
    /// before it (one third each).
    double change_rate = 0.01;
    /// Number of distinct line texts to draw from; 0 for lines that are
    /// almost all distinct. Small values give many repeated lines.
    std::size_t vocabulary = 0;
};

struct DocPair {
    std::string old_text;
    std::string new_text;
};

inline std::string random_line(Rng& rng, std::size_t vocabulary) {
    if (vocabulary > 0) return "line " + std::to_string(rng.index(vocabulary));
    static const char* const kWords[] = {"alpha", "bravo",  "charlie", "delta", "echo",   "foxtrot",
                                         "golf",  "hotel",  "india",   "juliet", "kilo",  "lima",
                                         "mike",  "november", "oscar", "papa"};
    std::string line;
    const std::size_t words = 2 + rng.index(5);
    for (std::size_t w = 0; w < words; ++w) {
        if (w > 0) line += ' ';
        line += kWords[rng.index(sizeof kWords / sizeof kWords[0])];
    }
    line += ' ';
    line += std::to_string(rng.below(1000000));
    return line;
}

/// An old document of `params.lines` lines and a new one derived from it.
/// Both end with a newline.
inline DocPair generate(const Params& params, std::uint64_t seed) {
    Rng rng(seed);
    DocPair docs;
    for (std::size_t i = 0; i < params.lines; ++i) {
        const std::string line = random_line(rng, params.vocabulary);
        docs.old_text += line;
        docs.old_text += '\n';
        if (!rng.chance(params.change_rate)) {
            docs.new_text += line;
            docs.new_text += '\n';
            continue;
        }
        switch (rng.index(3)) {
            case 0:  // delete
                break;
            case 1:  // replace
                docs.new_text += random_line(rng, params.vocabulary);
                docs.new_text += '\n';
                break;
            default:  // insert one to three lines before it
                for (std::size_t k = 1 + rng.index(3); k > 0; --k) {
                    docs.new_text += random_line(rng, params.vocabulary);
                    docs.new_text += '\n';
                }
                docs.new_text += line;
                docs.new_text += '\n';
                break;
        }
    }
    return docs;
}

}  // namespace diffcore::docgen
