# Changelog

## 0.1.0 - 2026-11-17

Initial release.

### Library

- `split_lines`: bytes to line views, remembering a missing final newline.
- `Interner` and `tokenize`: lines to integer tokens; equal hashes are
  compared in full, and a last line without its newline is a token of its
  own.
- `diff_tokens` and `diff`: minimal edit scripts with Myers' O(ND) greedy
  algorithm after trimming the common prefix and suffix, with changes slid
  into conventional positions, and a cost limit (`DiffOptions::max_cost`,
  `LimitError`).
- `make_hunks`: changes grouped into hunks with N lines of context, merged
  when at most 2N unchanged lines separate them.
- `format_unified`, `unified_diff`: unified diff output matching `diff -u`
  conventions, including `\ No newline at end of file`.
- `parse_patch`: multi-file unified diffs, `diff --git` and mail headers
  skipped, errors with patch line numbers (`ParseError`).
- `apply_patch`: exact-context apply with offset search, start/end anchoring
  of hunks near the ends of the file, reverse and dry-run modes, per-hunk
  results, and no output unless every hunk applies.
- `reversed`, `make_patch`, `make_file_patch`, `label_path`, `count_changes`.

### Tools

- `diffcore diff`, `stat`, `patch` and `bench`.
- `tools/make_golden.sh` (golden outputs from GNU diff) and
  `tools/compare_gnu.sh` (comparison with GNU diff and patch).
- `msvc/diffcore.vcxproj` for Visual Studio solutions, with a CTest check
  that keeps it in step with the sources.
- CMake presets for GCC/Clang, sanitizers, and MinGW-w64 cross builds
  (64-bit with tests under Wine, 32-bit build only).
