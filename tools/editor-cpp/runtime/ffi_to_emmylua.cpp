// STL ELŐSZÖR.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

#include "ffi_to_emmylua.h"

namespace editor::ffi_to_emmylua {

namespace fs = std::filesystem;

namespace {

// --- string helpers --------------------------------------------------------

std::string trim(std::string s) {
    auto ws = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && ws(s.front())) s.erase(s.begin());
    while (!s.empty() && ws(s.back()))  s.pop_back();
    return s;
}

std::string collapseWs(std::string s) {
    std::string out; out.reserve(s.size());
    bool lastSpace = false;
    for (char c : s) {
        if (std::isspace((unsigned char)c)) {
            if (!lastSpace && !out.empty()) out += ' ';
            lastSpace = true;
        } else {
            out += c;
            lastSpace = false;
        }
    }
    return trim(out);
}

bool startsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

// --- C-comment strip (only /* ... */, line-comments are nem szabványos C) --

std::string stripBlockComments(const std::string& src) {
    std::string out; out.reserve(src.size());
    size_t i = 0;
    while (i < src.size()) {
        if (i + 1 < src.size() && src[i] == '/' && src[i+1] == '*') {
            i += 2;
            while (i + 1 < src.size() && !(src[i] == '*' && src[i+1] == '/')) ++i;
            i += 2;   // skip closing */
        } else {
            out += src[i++];
        }
    }
    return out;
}

// --- Type-mapping: C-typedecl → EmmyLua type-string -----------------------

// A struct/typedef-class neveket gyűjtjük futás közben → ezek "class-name"-ek.
struct TypeContext {
    std::unordered_set<std::string> knownClasses;
    bool isClass(const std::string& n) const {
        return knownClasses.count(n) > 0;
    }
};

bool isIntegerKeyword(const std::string& w) {
    static const std::unordered_set<std::string> kw = {
        "int","long","short","char","signed","unsigned","bool",
        "size_t","ptrdiff_t","intptr_t","uintptr_t",
        "int8_t","int16_t","int32_t","int64_t",
        "uint8_t","uint16_t","uint32_t","uint64_t",
        "__int8","__int16","__int32","__int64",
        "GLenum","GLuint","GLint","GLboolean","GLbitfield","GLsizei",
        "GLshort","GLushort","GLbyte","GLubyte","GLubyte","GLfixed",
        "khronos_int8_t","khronos_int16_t","khronos_int32_t","khronos_int64_t",
        "khronos_uint8_t","khronos_uint16_t","khronos_uint32_t","khronos_uint64_t",
        "khronos_intptr_t","khronos_uintptr_t","khronos_ssize_t","khronos_usize_t",
    };
    return kw.count(w) > 0;
}

bool isFloatKeyword(const std::string& w) {
    static const std::unordered_set<std::string> kw = {
        "float","double","GLfloat","GLdouble","GLclampf","GLclampd",
        "khronos_float_t",
    };
    return kw.count(w) > 0;
}

bool isVoidKeyword(const std::string& w) { return w == "void"; }
bool isBoolKeyword(const std::string& w) { return w == "bool" || w == "_Bool"; }

// Egy C-típus-string EmmyLua-típusra. Pl.:
//   "const char*"     → "string"
//   "int"             → "integer"
//   "float"           → "number"
//   "void*"           → "lightuserdata"
//   "vec3"            → "vec3"     (registered class)
//   "struct obj *"    → "obj"
//   "void"            → "nil"
std::string mapType(std::string cType, const TypeContext& ctx) {
    std::string t = collapseWs(cType);

    // Strip qualifiers (jobb felismeréshez)
    static const std::vector<std::string> qualifiers = {
        "const", "volatile", "static", "extern", "register",
        "__stdcall", "__cdecl", "__fastcall", "__declspec(dllimport)",
    };
    for (const auto& q : qualifiers) {
        size_t pos;
        while ((pos = t.find(q)) != std::string::npos) {
            t.erase(pos, q.size());
        }
    }
    t = collapseWs(t);

    // Number of trailing pointers
    int ptrs = 0;
    while (!t.empty() && t.back() == '*') { t.pop_back(); ++ptrs; t = trim(t); }

    // struct/union prefix
    if (startsWith(t, "struct ")) t = t.substr(7);
    if (startsWith(t, "union "))  t = t.substr(6);
    if (startsWith(t, "enum "))   t = t.substr(5);
    t = trim(t);

    // Egyszerű kulcsszavak
    if (isVoidKeyword(t)) {
        if (ptrs > 0) return "lightuserdata";
        return "nil";
    }
    if (isBoolKeyword(t))    return ptrs > 0 ? "boolean[]" : "boolean";
    if (isIntegerKeyword(t)) return ptrs > 0 ? "integer[]" : "integer";
    if (isFloatKeyword(t))   return ptrs > 0 ? "number[]"  : "number";

    // char* / const char* → string
    if (t == "char" && ptrs >= 1)  return "string";
    if (t == "wchar_t" && ptrs >= 1) return "string";

    // Ismert class (registered)?
    if (ctx.isClass(t)) {
        if (ptrs > 0) return t;
        return t;
    }

    // Ismeretlen: ha pointer → opaque. Egyébként any.
    if (ptrs > 0) return "lightuserdata";
    return "any";
}

// --- Parser: paraméterlista felbontás zárójel-aware --------------------

// "int x, const char *s, vec3 v" → ["int x", "const char *s", "vec3 v"]
std::vector<std::string> splitArgs(const std::string& argsStr) {
    std::vector<std::string> out;
    int depth = 0;
    std::string cur;
    for (char c : argsStr) {
        if (c == '(' ) ++depth;
        if (c == ')' ) --depth;
        if (c == ',' && depth == 0) {
            std::string a = trim(cur);
            if (!a.empty()) out.push_back(a);
            cur.clear();
        } else {
            cur += c;
        }
    }
    std::string a = trim(cur);
    if (!a.empty()) out.push_back(a);
    return out;
}

// Egy paraméter-string ("const char *name") → (type, name).
struct Param { std::string type; std::string name; };
Param splitParam(const std::string& p) {
    Param out;
    std::string s = collapseWs(p);
    if (s.empty() || s == "void") return out;

    // Array-suffix kezelés: "int x[16]" → name="x", type="int*"
    bool isArray = false;
    size_t bracket = s.find('[');
    if (bracket != std::string::npos) {
        isArray = true;
        s = trim(s.substr(0, bracket));
    }

    // Az utolsó tokent a name-nek vesszük, kivéve ha csak típus van
    // (pl. "int" - név-nélküli paraméter).
    size_t lastSp = s.find_last_of(" \t*");
    if (lastSp == std::string::npos) {
        out.type = s;
        return out;
    }
    // A `*` is típushoz tartozik.
    std::string maybeName = s.substr(lastSp + 1);
    // Ha a maybeName-ben minden csak `*` vagy üres, akkor anonymous param.
    bool allStars = !maybeName.empty();
    for (char c : maybeName)
        if (c != '*') { allStars = false; break; }
    if (allStars) {
        out.type = s;
    } else {
        out.type = trim(s.substr(0, lastSp + 1));
        out.name = maybeName;
    }
    if (isArray) out.type += "*";
    return out;
}

// --- Function-prototype detect & extract -----------------------------------

struct ParsedFunc {
    std::string retType;
    std::string name;
    std::vector<Param> params;
};

// `(int x, ..., ...)` → split + map. Az `...` variadic skip.
bool parseFunction(const std::string& line, ParsedFunc& out) {
    // Egyszerű 1-soros minta: `<type> <name>(<args>);`
    static const std::regex re(
        R"(^\s*([A-Za-z_][A-Za-z0-9_\s\*]*[\s\*])\s*([A-Za-z_][A-Za-z0-9_]*)\s*\(([^()]*)\)\s*;\s*$)");
    std::smatch m;
    if (!std::regex_match(line, m, re)) return false;

    out.retType = collapseWs(m[1].str());
    out.name    = m[2].str();

    // Skip-list: nem motor-API, GL/SDL/X11 belső, debug-callback
    static const std::vector<std::string> skipPrefixes = {
        "gl", "PFNGL", "GL_", "glad", "GLAD",
        "__", "_",
    };
    for (const auto& p : skipPrefixes) {
        if (startsWith(out.name, p)) return false;
    }

    auto args = splitArgs(m[3].str());
    for (const auto& a : args) {
        if (a == "..." || a == "void") continue;
        out.params.push_back(splitParam(a));
    }
    return true;
}

// --- typedef struct extractor ---------------------------------------------

// `typedef struct NAME NAME;` vagy `typedef struct { ... } NAME;` minták.
// Egyszerűsítve: az utolsó identifier a typedef-név.
void collectTypedefStructs(const std::string& src, TypeContext& ctx) {
    static const std::regex re(
        R"(typedef\s+(?:struct|union|enum)\s+(?:[A-Za-z_][A-Za-z0-9_]*\s+)?([A-Za-z_][A-Za-z0-9_]*)\s*;)");
    auto begin = std::sregex_iterator(src.begin(), src.end(), re);
    auto end   = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        ctx.knownClasses.insert((*it)[1].str());
    }
    // Plus: `struct NAME { ... };`-szerű (előre-deklaráció).
    static const std::regex re2(
        R"(struct\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{)");
    begin = std::sregex_iterator(src.begin(), src.end(), re2);
    for (auto it = begin; it != end; ++it) {
        ctx.knownClasses.insert((*it)[1].str());
    }
}

// --- Formatter -------------------------------------------------------------

std::string formatFuncField(const ParsedFunc& f, const TypeContext& ctx) {
    std::ostringstream o;
    o << "---@field " << f.name << " fun(";
    for (size_t i = 0; i < f.params.size(); ++i) {
        const auto& p = f.params[i];
        std::string nm = p.name.empty() ? ("arg" + std::to_string(i + 1)) : p.name;
        // EmmyLua reserved? Egyszerű escape: `end`, `function`, `local`, ...
        static const std::unordered_set<std::string> resv = {
            "end","function","local","do","then","if","else","elseif",
            "repeat","until","while","for","return","break","in","not","and","or",
            "true","false","nil","goto"
        };
        if (resv.count(nm)) nm = "_" + nm;
        if (i) o << ", ";
        o << nm << ": " << mapType(p.type, ctx);
    }
    o << "): " << mapType(f.retType, ctx);
    return o.str();
}

}  // namespace

GenResult generate(const std::string& projectPath,
                   const std::string& ffiPath,
                   bool               force) {
    GenResult r;

    // 0) Fájl-paths.
    fs::path proj(projectPath);
    fs::path luarcDir   = proj / ".luarc";
    fs::path stubFile   = luarcDir / "engine.d.lua";
    fs::path configFile = proj / ".luarc.json";
    fs::path vscodeDir  = proj / ".vscode";
    fs::path vscodeFile = vscodeDir / "settings.json";
    r.stubPath   = stubFile.string();
    r.configPath = configFile.string();
    r.vscodePath = vscodeFile.string();

    std::error_code ec;

    // 1) Mtime-check (skip-on-up-to-date).
    if (!force && fs::exists(stubFile, ec) && fs::exists(ffiPath, ec)) {
        auto ffiM  = fs::last_write_time(ffiPath, ec);
        auto stubM = fs::last_write_time(stubFile, ec);
        if (!ec && ffiM <= stubM) {
            r.ok = true;
            return r;
        }
    }

    // 2) Read engine.ffi.
    std::ifstream in(ffiPath, std::ios::binary);
    if (!in) {
        r.error = "engine.ffi not readable: " + ffiPath;
        return r;
    }
    std::ostringstream ss; ss << in.rdbuf();
    std::string src = stripBlockComments(ss.str());

    // 3) Parse: collect type-context (classes) + funcs.
    TypeContext ctx;
    collectTypedefStructs(src, ctx);
    r.classes = (int)ctx.knownClasses.size();

    std::vector<ParsedFunc> funcs;
    {
        std::istringstream is(src);
        std::string line;
        while (std::getline(is, line)) {
            // Soron belüli többszöri deklaráció ritka — egysoros minta.
            ParsedFunc f;
            if (parseFunction(line, f)) funcs.push_back(std::move(f));
        }
    }
    r.functions = (int)funcs.size();

    // 4) Write outputs.
    fs::create_directories(luarcDir, ec);
    if (ec) { r.error = "create .luarc dir failed: " + ec.message(); return r; }

    // 4a) engine.d.lua
    {
        std::ofstream o(stubFile, std::ios::binary | std::ios::trunc);
        if (!o) { r.error = "stub file write failed"; return r; }
        o << "---@meta\n"
          << "-- Auto-generated from " << ffiPath << "\n"
          << "-- Do NOT edit by hand — regenerate via Tools → Generate Lua API Stubs.\n"
          << "-- " << funcs.size() << " functions, "
                   << ctx.knownClasses.size() << " classes.\n\n";

        // Class forward-declarations.
        // Néhány kézzel-hint-elt, hogy XYZ.x/y/z stb. autocomplete legyen.
        o << "---@class vec3\n"
          << "---@field x number\n"
          << "---@field y number\n"
          << "---@field z number\n\n"
          << "---@class vec2\n"
          << "---@field x number\n"
          << "---@field y number\n\n"
          << "---@class vec4\n"
          << "---@field x number\n"
          << "---@field y number\n"
          << "---@field z number\n"
          << "---@field w number\n\n"
          << "---@class quat\n"
          << "---@field x number\n"
          << "---@field y number\n"
          << "---@field z number\n"
          << "---@field w number\n\n"
          << "---@class obj\n\n";

        // Minden ismert (auto-felfedezett) class.
        std::vector<std::string> sortedClasses(
            ctx.knownClasses.begin(), ctx.knownClasses.end());
        std::sort(sortedClasses.begin(), sortedClasses.end());
        for (const auto& c : sortedClasses) {
            // skip ismétlés (vec3/vec2/vec4/quat/obj már fent).
            if (c == "vec3" || c == "vec2" || c == "vec4" || c == "quat"
                || c == "obj") continue;
            o << "---@class " << c << "\n";
        }
        o << "\n";

        // C globális namespace (`C.app_swap`, etc.) — `engine_C` class.
        o << "---@class engine_C\n";
        for (const auto& f : funcs) {
            o << formatFuncField(f, ctx) << "\n";
        }
        o << "---@type engine_C\nC = {}\n\n";

        // Globális self pointer (Script-komponens-LuaJIT-konvenció).
        o << "---@type obj\nself = nil\n\n";

        // ---- Math konstansok (runtime pre-loaded a Script-VM-ekben) ------
        o << "-- ===== Math konstansok =====\n"
          << "---@type number\ndeg2rad = nil    -- pi / 180\n"
          << "---@type number\nrad2deg = nil    -- 180 / pi\n\n";

        // ---- node helper modul (script_node_api.h-val szinkronban) -------
        // Ezek runtime-pre-loadolt Lua-shortcutok a Script-VM-ekben.
        o << "-- ===== `node` helper modul (runtime pre-loaded) =====\n"
          << "---@class engine_node\n"
          << "---@field pos          fun(o: obj|nil): vec3|nil\n"
          << "---@field rot          fun(o: obj|nil): vec3|nil\n"
          << "---@field scale        fun(o: obj|nil): vec3|nil\n"
          << "---@field parent       fun(o: obj|nil): obj|nil\n"
          << "---@field root         fun(o: obj|nil): obj|nil\n"
          << "---@field name         fun(o: obj|nil): string|nil\n"
          << "---@field type         fun(o: obj|nil): string|nil\n"
          << "---@field child_count  fun(o: obj|nil): integer\n"
          << "---@field child_at     fun(o: obj|nil, i: integer): obj|nil\n"
          << "---@field children     fun(o: obj|nil): fun(): obj|nil\n"
          << "---@field is_mesh      fun(o: obj|nil): boolean\n"
          << "---@field is_sprite    fun(o: obj|nil): boolean\n"
          << "---@field is_tilemap   fun(o: obj|nil): boolean\n"
          << "---@field is_light     fun(o: obj|nil): boolean\n"
          << "---@field is_camera    fun(o: obj|nil): boolean\n"
          << "---@field is_audio     fun(o: obj|nil): boolean\n"
          << "---@field is_script    fun(o: obj|nil): boolean\n"
          << "---@field mesh_path    fun(o: obj|nil): string|nil\n"
          << "---@field sprite_path  fun(o: obj|nil): string|nil\n"
          << "---@field tilemap_path fun(o: obj|nil): string|nil\n"
          << "---@field audio_path   fun(o: obj|nil): string|nil\n"
          << "---@field script_path  fun(o: obj|nil): string|nil\n"
          << "---@field camera_dir   fun(o: obj|nil): vec3|nil\n"
          << "---@type engine_node\nnode = {}\n";
    }

    // 4b) .luarc.json
    {
        std::ofstream o(configFile, std::ios::binary | std::ios::trunc);
        if (!o) { r.error = ".luarc.json write failed"; return r; }
        o << "{\n"
          << "    \"$schema\": \"https://raw.githubusercontent.com/sumneko/vscode-lua/master/setting/schema.json\",\n"
          << "    \"runtime\": {\n"
          << "        \"version\": \"LuaJIT\"\n"
          << "    },\n"
          << "    \"workspace\": {\n"
          << "        \"library\": [\"./.luarc\"],\n"
          << "        \"checkThirdParty\": false\n"
          << "    },\n"
          << "    \"diagnostics\": {\n"
          << "        \"globals\": [\"C\", \"self\", \"node\", \"deg2rad\", \"rad2deg\"]\n"
          << "    }\n"
          << "}\n";
    }

    // 4c) .vscode/settings.json — csak ha még NINCS ott (ne írjuk felül).
    if (!fs::exists(vscodeFile, ec)) {
        fs::create_directories(vscodeDir, ec);
        std::ofstream o(vscodeFile, std::ios::binary | std::ios::trunc);
        if (o) {
            o << "{\n"
              << "    \"Lua.runtime.version\": \"LuaJIT\",\n"
              << "    \"Lua.workspace.library\": [\"${workspaceFolder}/.luarc\"],\n"
              << "    \"Lua.workspace.checkThirdParty\": false,\n"
              << "    \"Lua.diagnostics.globals\": [\"C\", \"self\", \"node\"]\n"
              << "}\n";
        } else {
            r.vscodePath.clear();  // failure mark — ne lódítsuk a Console-t
        }
    } else {
        r.vscodePath.clear();  // skipped (kept user's edits)
    }

    r.ok = true;
    return r;
}

}  // namespace editor::ffi_to_emmylua
