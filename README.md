# Word Frequency

The open file's most frequent words as a bar chart in a tab of its own, in
[rhun](https://rhun.app). The chart follows your typing; hover a bar for its count, click a word to
select its first use. A small C program on
[rhunext.h](https://github.com/rhun-editor/extension-protocol/tree/main/c), one header with
nothing but the C library, and an example of rhun's views.

Install it from rhun: the extensions button in the title bar, **Install from GitHub…**, then
`rhun-editor/wordfreq`. Then run **Chart Word Frequency** from the palette.

## Permissions

It reads the files you have open. It changes nothing, has no internet and runs no other programs.

## Build

```sh
cc -std=c99 -O2 -o wordfreq main.c
tests/run.sh            # headless, against the rhun on your PATH
```

A tag `vX.Y.Z` equal to `version` in `extension.ini` builds Linux, macOS and Windows on their own
runners and publishes what Install from GitHub takes (section 11 of the spec).
