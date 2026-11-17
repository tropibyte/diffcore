// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Tropibyte, Inc.
//
// The public headers must compile after <windows.h> without NOMINMAX or
// WIN32_LEAN_AND_MEAN, which is how Windows applications include them.
// Built for MinGW targets.

#include <windows.h>

// MinGW's libstdc++ defines NOMINMAX, so there windows.h leaves out the min
// and max macros that it defines for MSVC; define them the same way. The
// standard headers come first because libstdc++'s, unlike MSVC's, are not
// written to survive those macros.
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "doctest.h"

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif

#include "diffcore/apply.hpp"
#include "diffcore/diff.hpp"
#include "diffcore/diffcore.hpp"
#include "diffcore/error.hpp"
#include "diffcore/hunks.hpp"
#include "diffcore/lines.hpp"
#include "diffcore/patch.hpp"
#include "diffcore/tokens.hpp"
#include "diffcore/unified.hpp"

TEST_CASE("the headers work after windows.h") {
    CHECK(max(1, 2) == 2);  // the macro is in force
    const std::string out = diffcore::unified_diff("a\n", "b\n", "a", "b");
    const std::string expected = "--- a\n+++ b\n@@ -1 +1 @@\n-a\n+b\n";
    CHECK(out == expected);
    CHECK(diffcore::kMaxLineNumber > 0);
}
