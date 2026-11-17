// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.
//
// diffcore: line diffs, unified diff output and patch apply. Includes every
// public header.

#pragma once

#include "diffcore/apply.hpp"
#include "diffcore/diff.hpp"
#include "diffcore/error.hpp"
#include "diffcore/hunks.hpp"
#include "diffcore/lines.hpp"
#include "diffcore/patch.hpp"
#include "diffcore/tokens.hpp"
#include "diffcore/unified.hpp"

namespace diffcore {

inline constexpr int kVersionMajor = 0;
inline constexpr int kVersionMinor = 1;
inline constexpr int kVersionPatch = 0;
inline constexpr const char* kVersion = "0.1.0";

}  // namespace diffcore
