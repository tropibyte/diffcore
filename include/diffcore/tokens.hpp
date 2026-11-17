// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.
//
// Interning lines as integer tokens, so the diff compares integers.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "diffcore/lines.hpp"

namespace diffcore {

using TokenId = std::uint32_t;

/// Hash function for Interner. It must be deterministic but need not be
/// collision-free: texts with equal hashes are compared in full.
using HashFunction = std::uint64_t (*)(std::string_view text) noexcept;

/// 64-bit FNV-1a.
std::uint64_t fnv1a_hash(std::string_view text) noexcept;

/// Gives every distinct line a dense id, starting at 0. A line without a
/// final newline gets a different id from the same text with one, so a diff
/// reports a change when only the final newline is added or removed.
///
/// Keys are views: the interned text must outlive the interner.
class Interner {
public:
    explicit Interner(HashFunction hash = fnv1a_hash);

    /// The id of `text` (with `incomplete` set for a line lacking its '\n'),
    /// assigning the next id if it is new. Throws LimitError past 2^32 - 2
    /// distinct lines.
    TokenId intern(std::string_view text, bool incomplete = false);

    std::size_t size() const noexcept { return keys_.size(); }
    std::string_view text(TokenId id) const { return keys_.at(id).text; }
    bool incomplete(TokenId id) const { return keys_.at(id).incomplete; }

private:
    struct Key {
        std::string_view text;
        bool incomplete = false;
    };
    struct Slot {
        std::uint64_t hash = 0;
        TokenId id = 0;  // kEmptySlot when unused
    };

    std::uint64_t hash_of(std::string_view text, bool incomplete) const noexcept;
    void rehash(std::size_t slot_count);

    HashFunction hash_;
    std::vector<Key> keys_;
    std::vector<Slot> slots_;
};

/// The ids of every line of `lines`, in order.
std::vector<TokenId> tokenize(const Lines& lines, Interner& interner);

}  // namespace diffcore
