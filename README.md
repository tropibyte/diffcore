# diffcore

Line-based diffs, unified diff output and patch apply, as a C++17 library
and a command-line tool.

diffcore computes minimal line diffs with Myers' O(ND) algorithm, writes
them in the unified format `diff -u` uses (byte for byte the same output
whenever GNU diff picks the same edit script), reads unified diffs back, and
applies them to text the way `patch` does with no fuzz: at the stated line
first, then searching nearby. It works on bytes, has no dependencies beyond
the standard library, and is written to build unchanged inside Visual
Studio solutions that use C++17 and `_HAS_STD_BYTE=0`.

```
$ diffcore diff --label checklist-v1.txt --label checklist-v2.txt examples/checklist-v1.txt examples/checklist-v2.txt
--- checklist-v1.txt
+++ checklist-v2.txt
@@ -3,15 +3,15 @@
 
 1. Update the version number in CMakeLists.txt.
 2. Update CHANGELOG.md with the release date.
-3. Build the dev, release and asan presets.
+3. Build the dev, release, asan and mingw presets.
 4. Run the test suite on every preset.
-5. Build the Visual Studio project in all four configurations.
-6. Tag the release.
-7. Push the tag.
+5. Run tools/compare_gnu.sh.
+6. Build the Visual Studio project in all four configurations.
+7. Tag the release.
+8. Push the tag.
 
 Notes
 -----
 
 Keep the changelog entries short.
 Mention breaking changes first.
-Link to the issues that were fixed.
```

## Features

- **Minimal diffs**: Myers' greedy algorithm over interned lines (each
  distinct line becomes an integer, compared by full text on hash
  collisions), after trimming the common prefix and suffix. Among equally
  short edit scripts, changes are slid into the positions diff readers
  expect: merged where possible, moved down past repeated lines, and kept
  next to the change they replace.
- **Unified diff output** with any amount of context, GNU's hunk header
  conventions (`-0,0` for an empty file, `,1` left out) and
  `\ No newline at end of file`.
- **Unified diff parser** for patches with several files, `diff --git` and
  mail headers around them, and precise errors (`line 12: more old lines
  than the hunk header's 3 lines`).
- **Patch apply** with per-hunk results (`Hunk #2 succeeded at 40 (offset 3
  lines).`), reverse and dry-run modes, and all-or-nothing output: if any
  hunk fails, nothing changes.
- **A cost limit** that turns a diff too large for the current algorithm
  into a clear error instead of exhausting memory.

## Building

Requirements: CMake 3.20+ and a C++17 compiler (GCC 11+, Clang 14+, MSVC 2022).

### Linux

```sh
cmake --preset dev            # Debug, warnings as errors
cmake --build --preset dev -j
ctest --preset dev
```

`CMakePresets.json` (CMake 3.21+) has these presets:

| Preset | What |
| --- | --- |
| `dev` | Debug, warnings as errors |
| `release` | Release |
| `asan` | Debug with AddressSanitizer and UBSan (not on Windows) |
| `mingw` | 64-bit Windows build with MinGW-w64; `ctest --preset mingw` runs the tests under Wine (Linux host) |
| `mingw32` | 32-bit Windows build with MinGW-w64, build only (Linux host) |

The MinGW presets use the `x86_64-w64-mingw32-g++-posix` and
`i686-w64-mingw32-g++-posix` cross compilers and link the runtimes
statically. The tests run with `wine64`, found on the `PATH` (the test
preset adds `/usr/lib/wine`, where Debian and Ubuntu install it). The
32-bit preset exists to catch code that assumes a 64-bit `size_t`.

Without presets: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`,
`cmake --build build`, `ctest --test-dir build`.

| Option | Default | Meaning |
| --- | --- | --- |
| `DIFFCORE_BUILD_TESTS` | ON when top-level | unit tests, golden tests, CLI tests and repository checks |
| `DIFFCORE_BUILD_CLI` | ON when top-level | the `diffcore` tool |
| `DIFFCORE_BUILD_EXAMPLES` | ON when top-level | `examples/diffcore_example.cpp` |
| `DIFFCORE_WARNINGS_AS_ERRORS` | OFF | `-Werror` / `/WX` |
| `DIFFCORE_SANITIZE` | OFF | AddressSanitizer + UBSan (GCC/Clang) |

### Windows, Visual Studio 2022

Open the folder (File > Open > Folder). Visual Studio reads
`CMakePresets.json` and offers the `dev` and `release` presets; pick one,
then Build > Build All and Test > Run CTests. From a Developer Command
Prompt the Linux commands above work as well.

### Adding the library to an existing Visual Studio solution

`msvc/diffcore.vcxproj` is a plain static library project (v143 toolset,
C++17, Unicode, Debug/Release for Win32 and x64) for solutions that do not
use CMake:

1. In Solution Explorer, right-click the solution, Add > Existing Project,
   and pick `msvc/diffcore.vcxproj`.
2. In the project that uses diffcore, right-click References (or
   Dependencies) > Add Reference, and tick `diffcore`. The library then
   builds first and links automatically.
3. In that project's Properties > C/C++ > General > Additional Include
   Directories, add the `include` folder, for example
   `$(SolutionDir)third_party\diffcore\include` (adjust to where you put
   the repository).

The project builds with `/W4 /permissive- /utf-8 /Zc:__cplusplus
/Zc:preprocessor`, defines `_HAS_STD_BYTE=0`, uses the static CRT
(`/MT`, `/MTd`) for x64 and the default DLL CRT for Win32, and writes
`diffcore.lib` to `$(SolutionDir)$(Platform)\$(Configuration)\`. Match the
runtime library of the project that links it. The public headers can be
included after `<windows.h>` without `NOMINMAX`.

## Command line

```
diffcore diff [-U N] [--label OLD --label NEW] [--max-cost N] OLD NEW
diffcore stat [-U N] [--max-cost N] OLD NEW
diffcore patch [-R] [--dry-run] [-o OUT] FILE PATCHFILE
diffcore bench --lines N --change-rate P [--seed S] [--vocab K] [--max-cost N] [--save OLD NEW]
```

`diff` and `stat` exit with 0 when the files are the same, 1 when they
differ and 2 on trouble, as `diff` does. `patch` exits with 0 when every
hunk applied, 1 when any failed (and then writes nothing) and 2 on trouble.

Without `--label`, the headers carry the file name and modification time
(in UTC):

```
$ diffcore diff examples/checklist-v1.txt examples/checklist-v2.txt
--- examples/checklist-v1.txt	2026-11-17 16:45:32.987238599 +0000
+++ examples/checklist-v2.txt	2026-11-17 16:45:32.998129019 +0000
@@ -3,15 +3,15 @@
...

$ diffcore stat examples/checklist-v1.txt examples/checklist-v2.txt
5 lines added, 5 lines removed, 1 hunk
```

`patch` reports hunks that applied away from their stated line, and
failures, in the words GNU patch uses:

```
$ diffcore patch checklist.txt examples/checklist.patch
patching file checklist.txt
Hunk #1 succeeded at 5 (offset 2 lines).

$ diffcore patch other.txt examples/checklist.patch
patching file other.txt
Hunk #1 FAILED at 3.
1 out of 1 hunk FAILED -- nothing written

$ diffcore patch -R --dry-run examples/checklist-v2.txt examples/checklist.patch
checking file examples/checklist-v2.txt
```

`bench` generates a document and an edited copy (deterministically, from
the seed) and times the diff. It is the baseline for performance work; a
Release build on a 2-CPU Linux VM gives:

```
$ diffcore bench --lines 100000 --change-rate 0.01
old: 100000 lines, new: 100283 lines
edit distance D: 1585 (651 deleted, 934 inserted), 901 hunks
diff time: 50.40 ms

$ diffcore bench --lines 20000 --change-rate 0.2
old: 20000 lines, new: 21357 lines
edit distance D: 6611 (2627 deleted, 3984 inserted), 930 hunks
diff time: 204.50 ms
```

`--save OLD NEW` writes the two documents to files, to time other tools on
the same input; `--vocab K` draws lines from K distinct texts, which makes
many equally short edit scripts.

## Using the library

```cpp
#include "diffcore/diffcore.hpp"

// Diff two texts: unified diff text, empty when they are equal.
const std::string patch_text = diffcore::unified_diff(old_text, new_text, "a/notes.txt", "b/notes.txt");

// Read a patch and apply its first file's hunks.
const diffcore::Patch patch = diffcore::parse_patch(patch_text);  // throws diffcore::ParseError
const diffcore::ApplyResult result = diffcore::apply_patch(target_text, patch.files.at(0));
if (result.ok()) {
    save(result.output);
} else {
    for (std::size_t i = 0; i < result.hunks.size(); ++i) {
        if (!result.hunks[i].applied) report_failed_hunk(i + 1, result.hunks[i].line);
    }
}
```

The steps are available separately: `split_lines`, `diff` (an
`EditScript` of Equal/Insert/Delete runs), `make_hunks`, `make_file_patch`
and `format_unified`; `diff_tokens` diffs any sequence of integer tokens.
All text is `std::string_view` / `std::string` holding bytes (UTF-8 or any
other 8-bit encoding). `examples/diffcore_example.cpp` is a complete
program. Link against `diffcore::diffcore` with CMake, or add the Visual
Studio project as above.

## Limitations

- **Line endings**: lines split at `\n` only, and a `\r` before it is part
  of the line. A file with CRLF line endings differs from the same text
  with LF on every line, and a patch with CRLF line endings does not apply
  to a file with LF endings (or the reverse).
- **Binary files** are diffed as text: NUL and other bytes are ordinary
  content. There is no "Binary files differ" check.
- **Large diffs**: the search keeps every step's frontier, so memory grows
  with the square of the edit distance D (about 2 x D^2 bytes, so 200 MB
  at the default limit of D = 10000). Beyond `max_cost` the diff stops
  with `LimitError` (the CLI prints `edit distance exceeds the limit of
  10000 lines` and exits with 2); raise it with `--max-cost` /
  `DiffOptions::max_cost`. Two unrelated files of more than 5000 lines each
  hit the default limit. Inputs where one side is empty once the common
  prefix and suffix are removed need no search and have no limit.
- **Ambiguous diffs**: when several edit scripts are equally short, diffcore
  and GNU diff may choose different ones. Both are minimal and have the
  same numbers of added and removed lines, but the text differs.
  `tools/compare_gnu.sh` measures how often (none of the differences
  involve inputs with all-distinct lines).
- **Patch apply** matches context exactly (fuzz 0): a hunk whose context
  lines were edited fails. There is no fuzz factor, no detection of
  patches that are already applied, and no `.rej` files.
- **Formats**: unified diffs only; no context (`-c`) or normal diff output
  or input.
- **No whitespace options** such as `-w`, `-b` or `--ignore-blank-lines`.
- **Files, not directories**: the CLI diffs two files (`is a directory`
  otherwise) and `diffcore patch` applies a patch for a single file: a patch
  with several file sections is rejected. Patches that create or delete
  files are applied to the FILE given, like any other.
- **CLI input** comes from named files; `-` does not mean standard input.
  Timestamps in headers are UTC (with whole seconds only where the C++
  runtime reports no more, as with MinGW). On Windows, file names on the
  command line pass through the ANSI code page.

## Project layout

```
include/diffcore/   public headers; diffcore.hpp includes them all
src/                line splitting, interning, Myers, hunks, unified format, apply
tools/              the diffcore tool, repository checks, golden and GNU comparison scripts
tests/              doctest unit tests, golden data, CLI tests (via CTest)
examples/           an example program and sample texts
msvc/               Visual Studio project for solutions that do not use CMake
cmake/              MinGW-w64 toolchain files
docs/               architecture notes
third_party/        doctest (MIT), used only by the tests
```

`tools/make_golden.sh` regenerates the expected outputs in
`tests/data/golden` with GNU diff, and `tools/compare_gnu.sh` checks
diffcore against GNU diff and patch on pseudo-random file pairs (Linux; not part of
`ctest`). On 360 file pairs it finds 0 failures in either direction of
apply, the same +/- counts as `diff --minimal -u` on all of them, and
byte-identical output to `diff -u` on 311.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [ROADMAP.md](ROADMAP.md)
and [CHANGELOG.md](CHANGELOG.md).

## License

MIT. Copyright (c) 2026 Tropibyte, Inc. See [LICENSE](LICENSE).
doctest is MIT-licensed; see `third_party/doctest/LICENSE.txt`.
