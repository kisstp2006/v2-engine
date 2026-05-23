#pragma once

// Lua "node" modul — pre-loaded minden Script-VM-be a ScriptHost::loadScript-ben.
// Egy ergonomikus wrapper a motor FFI editor_obj_* / editor_obj_is_* / obj_*
// függvényei köré. Mindenhol nil-safe (üres node-ra nem dob exception-t).
//
// Lua-ból elérhető példák:
//   local pos = node.pos(self)         -- vec3* (vagy nil)
//   local p   = node.parent(self)
//   local nm  = node.name(p)           -- Lua string
//   for child in node.children(p) do
//       print(node.name(child))
//   end
//   if node.is_mesh(p) then ... end

namespace editor {

// Megj.: a `local _C = C; local _ffi = require("ffi")` előbb tárolja, hogy
// a szkript-író felülírhassa a `C` globalt (pl. mock-hoz) anélkül hogy a
// node-modult is megrontaná.
constexpr const char* kNodeApiLua = R"LUA(
local _C       = C
local _ffi     = require("ffi")
local _string  = _ffi.string

-- ---- Math konstansok (Lua-globalok minden scriptben elérhetők) ----------
-- A Lua-standard `math.rad()` / `math.deg()` is használható ugyanezekkel:
--   math.rad(yaw) == yaw * deg2rad
--   math.deg(r)   == r   * rad2deg
deg2rad = math.pi / 180.0
rad2deg = 180.0 / math.pi

node = {}

-- ---- Transform-mezők (vec3* vagy nil) ----------------------------------
function node.pos(o)
    if not o then return nil end
    local p = _C.editor_obj_pos_addr(o)
    if p == nil then return nil end
    return p
end

function node.rot(o)
    if not o then return nil end
    local p = _C.editor_obj_rot_addr(o)
    if p == nil then return nil end
    return p
end

function node.scale(o)
    if not o then return nil end
    local p = _C.editor_obj_scale_addr(o)
    if p == nil then return nil end
    return p
end

-- ---- Hierarchia ---------------------------------------------------------
function node.parent(o)
    if not o then return nil end
    local p = _C.obj_parent(o)
    if p == nil then return nil end
    return p
end

function node.root(o)
    if not o then return nil end
    local p = _C.obj_root(o)
    if p == nil then return nil end
    return p
end

function node.child_count(o)
    if not o then return 0 end
    return _C.editor_obj_child_count(o)
end

function node.child_at(o, i)
    if not o then return nil end
    local c = _C.editor_obj_child_at(o, i)
    if c == nil then return nil end
    return c
end

-- Iterátor Lua for-loop-ban:  for child in node.children(parent) do end
function node.children(o)
    local n = node.child_count(o)
    local i = 0
    return function()
        if i >= n then return nil end
        local c = node.child_at(o, i)
        i = i + 1
        return c
    end
end

-- ---- Identitás (Lua-string) --------------------------------------------
function node.name(o)
    if not o then return nil end
    local n = _C.obj_name(o)
    if n == nil then return nil end
    return _string(n)
end

function node.type(o)
    if not o then return nil end
    local t = _C.obj_type(o)
    if t == nil then return nil end
    return _string(t)
end

-- ---- Type-check shortcutok ---------------------------------------------
local function _check(fn) return function(o)
    if not o then return false end
    return fn(o) ~= 0
end end

node.is_mesh    = _check(_C.editor_obj_is_mesh_renderer)
node.is_sprite  = _check(_C.editor_obj_is_sprite_renderer)
node.is_tilemap = _check(_C.editor_obj_is_tilemap_ref)
node.is_light   = _check(_C.editor_obj_is_light_ref)
node.is_camera  = _check(_C.editor_obj_is_camera_ref)
node.is_audio   = _check(_C.editor_obj_is_audio_source)
node.is_script  = _check(_C.editor_obj_is_script)

-- ---- Asset-path shortcutok (Lua-string, vagy nil) -----------------------
local function _path(fn) return function(o)
    if not o then return nil end
    local p = fn(o)
    if p == nil then return nil end
    return _string(p)
end end

node.mesh_path    = _path(_C.editor_mesh_renderer_path)
node.sprite_path  = _path(_C.editor_sprite_renderer_path)
node.tilemap_path = _path(_C.editor_tilemap_ref_path)
node.audio_path   = _path(_C.editor_audio_source_path)
node.script_path  = _path(_C.editor_script_path)

-- ---- Camera-specifikus (dir vec3*) -------------------------------------
function node.camera_dir(o)
    if not o then return nil end
    local d = _C.editor_camera_ref_dir_addr(o)
    if d == nil then return nil end
    return d
end
)LUA";

}  // namespace editor
