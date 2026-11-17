# diffcore architecture

```
old bytes ─► split_lines ─► Interner/tokenize ─► diff_tokens ─► EditScript ─► make_hunks ─► Hunk[]
new bytes ─►                                      (trim, Myers,                      │
                                                   compact)                          ▼
                                     format_unified ◄──────── FilePatch ◄── make_file_patch
                                           │                     ▲
                                           ▼                     │
                                      patch text ──► parse_patch ┘──► apply_patch(text) ─► ApplyResult
```

`FilePatch` is the one model of a patch: the writer formats it, the parser
produces it, and the applier consumes it, so a computed diff can be applied
without going through text and every test of one side exercises the other.

## Lines and tokens

`split_lines` (`lines.cpp`) returns views into the caller's buffer and a
flag for a missing final newline; nothing is copied. `Interner`
(`tokens.cpp`) maps each distinct line to a dense `TokenId` with an
open-addressing table (linear probing, at most half full) keyed by a
64-bit FNV-1a hash; equal hashes fall back to comparing the text, so
collisions cost time, never correctness. A test swaps in a constant hash
to prove it. The last line of a file without `\n` is interned with a flag,
which makes "b" and "b\n" different tokens: that is how adding or removing
the final newline shows up as a change, as in `diff -u`.

## The diff

`diff_tokens` (`myers.cpp`) works on token vectors:

1. **Trim** the common prefix and suffix. If either side is then empty, the
   middle is a pure insertion or deletion and no search runs.
2. **Search** with Myers' greedy algorithm (1986, section 3). Step d holds,
   for each diagonal k = x - y in -d, -d+2, ..., d, the furthest x reachable
   with d insertions and deletions; step d is derived from step d - 1 by a
   move down (insert) or right (delete) followed by the longest run of equal
   tokens. The search treats the grid as unbounded to the right and below,
   where nothing matches: such points can never lead back to (n, m), so the
   first step that reaches (n, m) is still the shortest, with no bounds
   checks in the loop. Every step's frontier is kept (d + 1 32-bit values)
   and the path is recovered by walking back from (n, m), repeating each
   step's choice. That is O((n + m) D) time and O(D^2) memory; the step
   count is checked against `max_cost` before each step allocates, and
   inputs whose middle exceeds 2^30 - 1 lines are refused, which keeps
   every position within a 32-bit `std::ptrdiff_t`.
3. **Compact** the result. The search marks changed lines in each file; a
   run of changed lines can trade places with an equal unchanged neighbour
   without changing the edit distance. For each file, each run is pushed up
   and down as far as it goes, absorbing runs it meets, until it stops
   growing; it then rests at the lowest position that faces a change in the
   other file (unchanged lines pair up in order, so "gap g" of one file
   faces gap g of the other), or else at the lowest position. The old file
   is compacted first, then the new file against the result.
4. **Build the script** from the two flag arrays: Equal runs where both
   sides are unchanged, then Delete before Insert.

The output is minimal (a test checks the distance against a brute-force
LCS on thousands of random inputs) and normalized: runs are maximal and
non-empty.

## Hunks and the unified format

`make_hunks` (`hunks.cpp`) walks the script's change runs (consecutive
non-Equal edits) and joins neighbours separated by at most 2 x context
unchanged lines, so contexts that would touch or overlap share a hunk.
Leading and trailing context is clipped at the ends of the file. Starts
are 0-based; for an empty side, the start is the line the hunk's other
lines go before. `format_range` turns that into the header form: `l` for
one line, `l,s` otherwise, and the line before the range for `s = 0`.

`parse_patch` (`unified_parse.cpp`) reads line by line. Text outside
`--- ` / `+++ ` / `@@` structure is skipped; inside a hunk, the header's
counts decide where the hunk ends, so a `-` or `+` line past them is an
error rather than the next file's header. `\ No newline at end of file`
sets a flag on the line before it, and is checked to follow the last line
of the old text, the new text, or both. Errors carry 1-based patch line
numbers.

## Applying

`apply_patch` (`apply.cpp`) splits the target into lines and places hunks
in order. A hunk's old text (context and removed lines) must match exactly,
including the newline flag of the last line. The search starts at the
stated position shifted by the offset of the last hunk that applied, and
tries distance 0, +1, -1, +2, -2... but never above the end of the
previous hunk's match. Hunks with asymmetric context come from the ends of
a file, so they are anchored: less trailing than leading context means
"at the end", less leading than trailing context with a stated line of 1
means "at the start". Hunks without old lines are placed, not searched
for. The output is assembled from byte ranges of the input and the hunks'
new lines only when every hunk applied; otherwise `output` stays empty.

Reported line numbers follow `patch`: an applied hunk's line is where its
text starts in the output; a failed hunk's line is its stated line moved by
the lines that earlier hunks added or removed.

## Portability

The library is C++17 with no platform code; it compiles with
`_HAS_STD_BYTE=0` (`tools/check_no_std_byte.cmake` keeps `std::byte` out
of the sources on every compiler) and its headers survive `<windows.h>`'s
`min`/`max` macros (`tests/test_windows_h.cpp`, built for MinGW, defines
them and includes every header). Sizes are `std::size_t`; signed
arithmetic on positions uses `std::ptrdiff_t` with inputs bounded so it
cannot overflow on 32-bit targets, which the `mingw32` preset builds with
warnings as errors. `msvc/diffcore.vcxproj` lists the sources by hand;
`tools/check_vcxproj.cpp` parses it and its `.filters` file in a CTest
test and fails when they drift from `src/` and `include/diffcore/` or lose
their required settings.

## Adding things

- **A diff option** (such as ignoring whitespace) usually belongs in
  tokenization: intern a normalized key instead of the raw line, keep the
  raw lines for output. Add the field to `DiffOptions`.
- **Another algorithm**: produce changed-line flags for the trimmed middle
  and reuse compaction and `script_from_flags` in `myers.cpp`, so output
  stays normalized.
- **A source file**: add it to `CMakeLists.txt` and to both files in
  `msvc/`; the `vcxproj_in_sync` test fails until you do.
- **A golden case**: add `NAME.old` and `NAME.new` to `tests/data/golden`
  and run `tools/make_golden.sh`; choose inputs with a unique minimal edit
  script so the expected output does not depend on tie-breaking.
