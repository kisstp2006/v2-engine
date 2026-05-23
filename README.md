# v2 — game engine + Unity-style editor

Egy C/C++ alapú, lightweight 3D + 2D game engine, és egy Dear ImGui alapú,
Unity-stílusú editor.

## Komponensek

| Mappa | Mit ad | License |
|---|---|---|
| `code/` | A motor magja (render, audio, input, scene-graph, LuaJIT FFI, reflection) | **MIT** |
| `ext/` | Backend pluginek (SDL3, OpenAL, ENet, ImGui-glue, FFmpeg-audio, stb.) | **MIT** (lásd egyes alkönyvtárakat) |
| `demos/` | Példa-játékok és use-case-ek a motor API-jára | **MIT** |
| `tools/editor-cpp/` | Unity-stílusú scene-editor (Hierarchy/Inspector/Project/Scene/Game/Console/Build panelekkel) | **GPL-3.0-or-later** |

## Build

```bat
:: Motor + hello.exe (a fő engine binary):
MAKE.bat

:: Editor (editor-cpp.exe):
tools\editor-cpp\BUILD.bat
```

Mind kettő MSVC `cl.exe`-t használ (Visual Studio 2022 Community ajánlott).
A `BUILD.bat` rekurzívan auto-discover-eli a `tools/editor-cpp/` alatti
`.c` és `.cpp` fájlokat — új modul hozzáadása nem igényel build-config-
módosítást.

## Editor — gyors túra

- **Hierarchy panel** — scene-tree, drag-drop reparenting, Delete-key törlés
- **Inspector** — reflection-vezérelt mező-edit (`STRUCT()` macros + hint-string-ek)
- **Project panel** — `assets/` mappa-böngésző, Import gomb, double-click spawn, asset-preview
- **Scene + Scene 2D + Game panel** — 3D viewport, 2D viewport, Play-mode
- **Build panel** — Cook + zip + AssetValidator progress (threaded)
- **Toolbar** — Play / Pause / Stop, gizmo-mode switcher

Komponens-típusok: Transform, MeshRenderer, SpriteRenderer, TilemapRef,
LightRef (DIR/POINT/SPOT + shadows), CameraRef, AudioSource (spatial),
Script (Lua, hot-reload).

## Lua scripting

- Minden Script-komponens egy izolált LuaJIT VM-et kap (`script_init_env`)
- Az `engine.ffi` (~21k sor cdef) a teljes motor API-t exportálja `C.*`-on
- A `node.*` helper-modul (`tools/editor-cpp/runtime/script_node_api.h`)
  shortcut-okat ad: `node.pos(o)`, `node.rot(o)`, `node.parent(o)`,
  `node.is_mesh(o)`, stb.
- A `.luarc/engine.d.lua` EmmyLua-stub auto-generálva (Tools menü) → VSCode
  + JetBrains EmmyLua IntelliSense + hover-tooltip a `C.*` API-ra
- Hot-reload mtime-poll-lal — fájl-mentés azonnal újratölti a Play-mode VM-et

## Asset-pipeline

- Recipe-rendszer (`code/sys/sys_cook2.h`) — external command-converterek
  (pl. `ffmpeg.EXE` mp3 → ogg)
- Cook + cook.zip build (Tools menü, threaded)
- AssetValidator — pre-cook check a scene-asset-path-okra (létezés,
  extension-match, projekt-relatív path)

## Modellformátum-támogatás

- **IQM** (Inter-Quake Model) — natív, animation + skinning
- **glTF / GLB** (PBR materials, statikus mesh) — via cgltf (`code/3rd/3rd_cgltf.h`)

A felhasználói projekt-mappa-strukturat a `New Project` wizard hozza
létre (`project.json5` + `assets/{models,scenes,scripts,prefabs}/`).

## License notes

- A **motor** (`code/`, `ext/`, `demos/`) MIT alatt — szabadon felhasználható
  closed-source projektekben is.
- Az **editor** (`tools/editor-cpp/`) GPL-3.0-or-later alatt — a forkok és
  származékos művek kötelesek nyitott-forráskódúak maradni. A copyright-
  tulajdonos (tibig) fenntartja a jogot, hogy más license-szel adja ki
  (pl. kereskedelmi / closed-source).
