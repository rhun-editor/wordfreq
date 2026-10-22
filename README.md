# Word Frequency

The open file's most frequent words as a bar chart in a tab of its own, in
[rhun](https://rhun.app). The chart follows your typing; hover a bar for its count, click a word to
select its first use. A small C program on
[rhunext.h](https://github.com/rhun-editor/extension-protocol/tree/main/c) (v1.4.0, copied
here), one header with nothing but the C library, and an example of rhun's views.

Install it from rhun: the extensions button in the title bar, **Install from GitHub…**, then
`rhun-editor/wordfreq`. Then run **Chart Word Frequency** from the palette.

It needs rhun 0.18, and shows two parts of its extension protocol 1.1 that follow the active
file as you type:

- **A table rhun keeps** (a retained view, features `view-tree` and `view-table`): the
  **Word Frequency** tab of the bottom panel (Ctrl+J) lists the file's words with their uses and
  first line. The extension sends the rows once per change; rhun draws them, sorts by a click on
  a column's header and scrolls, and a click on a row (or Enter) selects the word's first use
  through the row's `open` record. Showing the tab starts the extension.
- **A status bar item** (`status-items`): the file's word count, the most frequent words in its
  tooltip; a click opens the chart. The manifest's `[status]` item of the same name shows before
  the program runs and starts it.

## Permissions

It reads the files you have open. It changes nothing, has no internet and runs no other programs.

## Build

```sh
cc -std=c99 -O2 -o wordfreq main.c
tests/run.sh            # headless, against the rhun on your PATH: tests/basic.txt, tests/table.txt
```

A tag `vX.Y.Z` equal to `version` in `extension.ini` builds Linux, macOS and Windows on their own
runners and publishes what Install from GitHub takes (section 11 of the spec).
