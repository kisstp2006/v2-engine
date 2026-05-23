# v2 — game engine + scene editor

A lightweight C/C++ based 3D + 2D game engine, plus a Dear ImGui based
scene editor (Hierarchy / Inspector / Project / Scene / Game / Console /
Build panels).

## Components

| Folder | What it provides | License |
|---|---|---|
| `code/` | The engine core (render, audio, input, scene-graph, LuaJIT FFI, reflection) | **Apache 2.0** |
| `ext/` | Backend plugins (SDL3, OpenAL, ENet, ImGui-glue, FFmpeg-audio, etc.) | **Apache 2.0** (see individual subdirectories) |
| `demos/` | Sample games and use-cases for the engine API | **Apache 2.0** |
| `tools/editor-cpp/` | Scene editor (Hierarchy / Inspector / Project / Scene / Game / Console / Build panels) | **GPL-3.0-or-later** |

## Build

```bat
:: Engine + hello.exe (the main engine binary):
MAKE.bat

:: Editor (editor-cpp.exe):
tools\editor-cpp\BUILD.bat
```

Both use MSVC `cl.exe` (Visual Studio 2022 Community recommended).
`BUILD.bat` recursively auto-discovers the `.c` and `.cpp` files under
`tools/editor-cpp/` — adding a new module does not require changing the
build config.

## Editor — quick tour

- **Hierarchy panel** — scene tree, drag-drop reparenting, Delete-key removal
- **Inspector** — reflection-driven field-edit (`STRUCT()` macros + hint strings)
- **Project panel** — `assets/` folder browser, Import button, double-click spawn, asset-preview
- **Scene + Scene 2D + Game panel** — 3D viewport, 2D viewport, Play-mode
- **Build panel** — Cook + zip + AssetValidator progress (threaded)
- **Toolbar** — Play / Pause / Stop, gizmo-mode switcher

Component types: Transform, MeshRenderer, SpriteRenderer, TilemapRef,
LightRef (DIR/POINT/SPOT + shadows), CameraRef, AudioSource (spatial),
Script (Lua, hot-reload).

## Lua scripting

- Every Script component gets an isolated LuaJIT VM (`script_init_env`)
- The `engine.ffi` (~21k lines of cdef) exposes the full engine API on `C.*`
- The `node.*` helper module (`tools/editor-cpp/runtime/script_node_api.h`)
  provides shortcuts: `node.pos(o)`, `node.rot(o)`, `node.parent(o)`,
  `node.is_mesh(o)`, etc.
- The `.luarc/engine.d.lua` EmmyLua stub is auto-generated (Tools menu) →
  VSCode + JetBrains EmmyLua IntelliSense + hover-tooltip for the `C.*` API
- Hot-reload via mtime-poll — a file save immediately re-loads the Play-mode VM

## Asset pipeline

- Recipe system (`code/sys/sys_cook2.h`) — external command converters
  (e.g. `ffmpeg.EXE` mp3 → ogg)
- Cook + cook.zip build (Tools menu, threaded)
- AssetValidator — pre-cook check on scene asset paths (existence,
  extension match, project-relative path)

## Model format support

- **IQM** (Inter-Quake Model) — native, animation + skinning
- **glTF / GLB** (PBR materials, static mesh) — via cgltf (`code/3rd/3rd_cgltf.h`)

The user-project folder structure is created by the `New Project` wizard
(`project.json5` + `assets/{models,scenes,scripts,prefabs}/`).

## License notes

- The **engine** (`code/`, `ext/`, `demos/`) is licensed under the
  **Apache License 2.0** — freely usable in closed-source projects too,
  including patent-grant protection (the "professional open-source"
  preferred license).
- The **editor** (`tools/editor-cpp/`) is licensed under **GPL-3.0-or-later** —
  forks and derivative works must remain open-source. The copyright holder
  (TIGames) reserves the right to release it under other licenses (e.g.
  commercial / closed-source).
