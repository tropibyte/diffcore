# Roadmap

Planned work. Nothing here is implemented yet.

## Algorithms

- **Patience diff**: anchor the diff on lines that occur exactly once in
  each file, then diff between the anchors. Often reads better on source
  code where blocks move or braces repeat.
- **Histogram diff**: patience's idea extended to lines that occur a few
  times, choosing the rarest lines as anchors.
- **Linear-space Myers**: divide and conquer on the middle snake (section
  4b of Myers' paper), so memory grows with the input size rather than the
  square of the edit distance. This removes the need for the `max_cost`
  limit in everyday use (it would remain as a time bound).
- **Speed for large files**: discard lines that occur in only one file
  before the search, keep the line table and frontier buffers between
  calls, and add a benchmark target with fixed inputs and recorded results
  to track (built on `diffcore bench`).

## Merging

- **Three-way merge** (`diff3`): merge two sets of changes to a common base,
  with conflict markers (`<<<<<<<`, `|||||||`, `=======`, `>>>>>>>`) where
  they overlap, and a library API that reports conflicts as data.

## Input handling

- **CRLF handling**: options to ignore or normalize line-ending
  differences when comparing, and to keep each file's line endings when
  writing and applying patches (so a patch made on LF files applies to
  CRLF files).
- **Binary file detection**: report "Binary files OLD and NEW differ"
  for files containing NUL bytes (or failing a text heuristic) instead of
  diffing them line by line.
- **Whitespace options**: `-w` (ignore all white space), `-b` (ignore
  changes in the amount of white space) and `--ignore-blank-lines`.

## Patch apply

- **Fuzz factor**: like `patch -F N`, retry a failing hunk ignoring up to N
  lines of context at its ends, and report "succeeded at L with fuzz N".

## Output formats and scope

- **Context diffs** (`-c`) and the **normal** diff format, for output and
  for patch input.
- **Directory diffs** (`-r`): walk two trees, pair files by path, report
  files only in one tree, and write one multi-file patch.
- **Word-level and intra-line diffs**: diff the words or characters of
  changed line pairs (the token diff, `diff_tokens`, is already
  independent of lines) and mark the changed spans.
