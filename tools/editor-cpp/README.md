# editor-cpp

Dear ImGui based C++ scene editor for the v2 engine.

## Build

In the `tools/editor-cpp/` folder, run:

```bat
BUILD.bat
```

First run (cold build): ~30-60 sec, because `MAKE.bat` also compiles the engine's `native.obj` and `backend.obj` cache. Subsequent runs: ~5-10 sec.

## Run

From the repo root:

```bat
editor-cpp.exe
```

`ESC`: quit.

## Features

- Hierarchy / Inspector / Project / Scene / Scene 2D / Game / Console / Build panels
- Reflection-driven Inspector with hint-string controls (`[range]`, `[color3]`, `[multiline]`, `[asset:...]`, ...)
- Translate / Rotate / Scale gizmos (W / E / R hotkeys)
- Multi-select + undo/redo (Ctrl+Z / Ctrl+Y)
- Play / Pause / Stop with snapshot-restore
- Drag-drop reparenting in Hierarchy, drag-drop asset spawn in Scene panels
- JSON5 scene persistence
- Prefab clone + save
- Lua scripting with hot-reload (per-Script LuaJIT VM, `engine.ffi` + `node.*` helpers)
- Project cook + AssetValidator (threaded, with progress UI)
- VSCode / JetBrains EmmyLua IntelliSense via auto-generated `engine.d.lua`

Built-in component types: Transform, MeshRenderer, SpriteRenderer, TilemapRef,
LightRef (DIR / POINT / SPOT + shadows), CameraRef, AudioSource (spatial), Script.
