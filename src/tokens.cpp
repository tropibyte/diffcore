// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.

#include "diffcore/tokens.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

#include "diffcore/error.hpp"

namespace diffcore {

namespace {

constexpr TokenId kEmptySlot = (std::numeric_limits<TokenId>::max)();
constexpr std::size_t kMaxTokens = static_cast<std::size_t>(kEmptySlot) - 1;
constexpr std::uint64_t kIncompleteSalt = 0x9E3779B97F4A7C15ULL;

std::size_t slot_index(std::uint64_t hash, std::size_t mask) noexcept {
    // Fold the high half in so 32-bit size_t still sees all of the hash.
    return static_cast<std::size_t>(hash ^ (hash >> 32)) & mask;
}

}  // namespace

std::uint64_t fnv1a_hash(std::string_view text) noexcept {
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (const char c : text) {
        h ^= static_cast<unsigned char>(c);
        h *= 0x100000001B3ULL;
    }
    return h;
}

Interner::Interner(HashFunction hash) : hash_(hash) { rehash(64); }

std::uint64_t Interner::hash_of(std::string_view text, bool incomplete) const noexcept {
    const std::uint64_t h = hash_(text);
    return incomplete ? h ^ kIncompleteSalt : h;
}

TokenId Interner::intern(std::string_view text, bool incomplete) {
    const std::uint64_t h = hash_of(text, incomplete);
    const std::size_t mask = slots_.size() - 1;
    std::size_t i = slot_index(h, mask);
    for (;; i = (i + 1) & mask) {
        Slot& slot = slots_[i];
        if (slot.id == kEmptySlot) break;
        if (slot.hash == h) {
            const Key& key = keys_[slot.id];
            if (key.incomplete == incomplete && key.text == text) return slot.id;
        }
    }
    if (keys_.size() >= kMaxTokens) throw LimitError("too many distinct lines to diff");
    const auto id = static_cast<TokenId>(keys_.size());
    keys_.push_back(Key{text, incomplete});
    slots_[i] = Slot{h, id};
    // Keep the table at most half full.
    if (keys_.size() * 2 > slots_.size()) rehash(slots_.size() * 2);
    return id;
}

void Interner::rehash(std::size_t slot_count) {
    std::vector<Slot> slots(slot_count, Slot{0, kEmptySlot});
    const std::size_t mask = slot_count - 1;
    for (const Slot& old : slots_) {
        if (old.id == kEmptySlot) continue;
        std::size_t i = slot_index(old.hash, mask);
        while (slots[i].id != kEmptySlot) i = (i + 1) & mask;
        slots[i] = old;
    }
    slots_.swap(slots);
}

std::vector<TokenId> tokenize(const Lines& lines, Interner& interner) {
    std::vector<TokenId> ids;
    ids.reserve(lines.size());
    for (std::size_t i = 0; i < lines.size(); ++i) ids.push_back(interner.intern(lines[i], lines.is_incomplete(i)));
    return ids;
}

}  // namespace diffcore
