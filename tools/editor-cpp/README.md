# editor-cpp

Unity-stílusú C++ + Dear ImGui szerkesztő a v2 motorhoz. Tervfájl: `~/.claude/plans/fizzy-jingling-bumblebee.md`.

## Build

A `tools/editor-cpp/` mappában futtasd:

```bat
BUILD.bat
```

Első futás (hideg build): ~30-60 mp, mert a `MAKE.bat` lefordítja a motor `native.obj` és `backend.obj` cache-jét is. Következő futás: ~5-10 mp.

## Run

A repo gyökeréből:

```bat
editor-cpp.exe
```

`ESC`: kilép.

## Status

**M0a** — bootstrap kész. Üres OS ablak nyílik. Még nincs ImGui UI, nincs custom theme.

Következő: **M0b** — custom theme + B612 font + Material Symbols.
