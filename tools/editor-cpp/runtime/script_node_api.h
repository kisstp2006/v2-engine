#pragma once

// Lua "node" module — pre-loaded into every Script-VM in ScriptHost::loadScript.
// An ergonomic wrapper around the engine's FFI editor_obj_* / editor_obj_is_* /
// obj_* functions. Everywhere nil-safe (doesn't throw exceptions on an empty node).
//
// Examples accessible from Lua:
//   local pos = node.pos(self)         -- vec3* (or nil)
//   local p   = node.parent(self)
//   local nm  = node.name(p)           -- Lua string
//   for child in node.children(p) do
//       print(node.name(child))
//   end
//   if node.is_mesh(p) then ... end

namespace editor {

// Note: the `local _C = C; local _ffi = require("ffi")` stores them first,
// so that the script author can override the `C` global (e.g. for mocking)
// without also breaking the node module.
constexpr const char* kNodeApiLua = R"LUA(
local _C       = C
local _ffi     = require("ffi")
local _string  = _ffi.string

-- ---- Math constants (Lua-globals accessible in every script) ----------
-- The Lua-standard `math.rad()` / `math.deg()` can also be used with these:
--   math.rad(yaw) == yaw * deg2rad
--   math.deg(r)   == r   * rad2deg
deg2rad = math.pi / 180.0
rad2deg = 180.0 / math.pi

node = {}

-- ---- Transform fields (vec3* or nil) ----------------------------------
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

-- ---- Hierarchy ---------------------------------------------------------
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

-- Iterator in a Lua for-loop:  for child in node.children(parent) do end
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

-- ---- Identity (Lua-string) --------------------------------------------
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

-- ---- Type-check shortcuts ---------------------------------------------
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
node.is_fog     = _check(_C.editor_obj_is_fog_settings)
node.is_skybox  = _check(_C.editor_obj_is_skybox)
node.is_text    = _check(_C.editor_obj_is_text_renderer)
node.is_text3d  = _check(_C.editor_obj_is_text_renderer_3d)

-- ---- Asset-path shortcuts (Lua-string, or nil) -----------------------
local function _path(fn) return function(o)
    if not o then return nil end
    local p = fn(o)
    if p == nil then return nil end
    return _string(p)
end end

node.mesh_path        = _path(_C.editor_mesh_renderer_path)
node.sprite_path      = _path(_C.editor_sprite_renderer_path)
node.tilemap_path     = _path(_C.editor_tilemap_ref_path)
node.audio_path       = _path(_C.editor_audio_source_path)
node.script_path      = _path(_C.editor_script_path)
node.skybox_sky_path  = _path(_C.editor_skybox_sky_path)
node.skybox_refl_path = _path(_C.editor_skybox_refl_path)
node.skybox_env_path  = _path(_C.editor_skybox_env_path)
node.text_str         = _path(_C.editor_text_renderer_text)
node.text3d_str       = _path(_C.editor_text_renderer_3d_text)

-- ---- Camera-specific (dir vec3*) -------------------------------------
function node.camera_dir(o)
    if not o then return nil end
    local d = _C.editor_camera_ref_dir_addr(o)
    if d == nil then return nil end
    return d
end

-- ---- Fog field-pointer accessors (mutable) ---------------------------
-- Same pattern as node.pos / node.rot / node.scale — return a pointer the
-- script can mutate in-place. Example:
--   local color = node.fog_color(fog)
--   color.x = 0.5 + 0.5 * math.sin(os.clock())
function node.fog_mode(o)
    if not o then return nil end
    local p = _C.editor_fog_settings_mode_addr(o)
    if p == nil then return nil end
    return p
end
function node.fog_color(o)
    if not o then return nil end
    local p = _C.editor_fog_settings_color_addr(o)
    if p == nil then return nil end
    return p
end
function node.fog_start(o)
    if not o then return nil end
    local p = _C.editor_fog_settings_start_addr(o)
    if p == nil then return nil end
    return p
end
function node.fog_end(o)
    if not o then return nil end
    local p = _C.editor_fog_settings_end_addr(o)
    if p == nil then return nil end
    return p
end
function node.fog_density(o)
    if not o then return nil end
    local p = _C.editor_fog_settings_density_addr(o)
    if p == nil then return nil end
    return p
end

-- ---- Scene-traversal helpers -----------------------------------------
-- `scene.find_first(root, predicate)` is a generic depth-first search that
-- returns the first node where `predicate(node)` is truthy.
-- Convenience: `scene.find_fog(root)` returns the first FogSettings node.
--
-- Example (from a Script attached anywhere in the scene):
--   function on_update(self, dt)
--       local fog = scene.find_fog(node.root(self))
--       if fog then node.fog_density(fog)[0] = 0.05 end
--   end
scene = scene or {}

function scene.find_first(root, predicate)
    if not root or not predicate then return nil end
    if predicate(root) then return root end
    for child in node.children(root) do
        local r = scene.find_first(child, predicate)
        if r then return r end
    end
    return nil
end

function scene.find_fog(root)
    return scene.find_first(root, node.is_fog)
end

function scene.find_skybox(root)
    return scene.find_first(root, node.is_skybox)
end

function scene.find_text(root)
    return scene.find_first(root, node.is_text)
end

function scene.find_text3d(root)
    return scene.find_first(root, node.is_text3d)
end

-- `scene.find_all(root, predicate)` collects every matching node into a table.
-- Useful when there can be many of the same type (e.g. all Text3D labels).
function scene.find_all(root, predicate)
    local out = {}
    if not root or not predicate then return out end
    local function recurse(n)
        if predicate(n) then out[#out + 1] = n end
        for child in node.children(n) do recurse(child) end
    end
    recurse(root)
    return out
end

-- ---- Skybox render-background toggle (int*, mutable) ------------------
function node.skybox_render_bg(o)
    if not o then return nil end
    local p = _C.editor_skybox_render_bg_addr(o)
    if p == nil then return nil end
    return p
end

-- ---- TextRenderer (HUD) field-pointer accessors (mutable) ------------
function node.text_face(o)
    if not o then return nil end
    local p = _C.editor_text_renderer_face_addr(o)
    if p == nil then return nil end
    return p
end
function node.text_color(o)
    if not o then return nil end
    local p = _C.editor_text_renderer_color_addr(o)
    if p == nil then return nil end
    return p
end
function node.text_size(o)
    if not o then return nil end
    local p = _C.editor_text_renderer_size_addr(o)
    if p == nil then return nil end
    return p
end
function node.text_max_width(o)
    if not o then return nil end
    local p = _C.editor_text_renderer_max_width_addr(o)
    if p == nil then return nil end
    return p
end

-- ---- Text3DRenderer field-pointer accessors (mutable) ----------------
-- Use `node.scale(o)` for the size — Text3D's scale.x is the uniform
-- ddraw_text size multiplier. Only the color override is exposed here.
function node.text3d_color(o)
    if not o then return nil end
    local p = _C.editor_text_renderer_3d_color_addr(o)
    if p == nil then return nil end
    return p
end
)LUA";

}  // namespace editor
