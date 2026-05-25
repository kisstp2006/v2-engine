#pragma once

// Editor-szintű component-extra-serializer. A v2 motor `obj_saveini` reflection
// rendszere NEM támogat array() vagy összetett típusú mezőket — ha egy
// komponensnek van runtime-array (pl. MeshRenderer.material_overrides) vagy
// más nem-trivit-szerializálható mezője, akkor a komponens-típushoz
// regisztrál egy IComponentExtraSerializer-t, ami a v2-szerializáció UTÁN
// fűz egy JSON-blob-ot a node-body végéhez.
//
// Format (a node-body végén):
//   [Foo] ; v100
//   int.x=42
//   char*.name=hello
//   ___EDITOR_EXTRAS_V1___
//   <serializer-által generált JSON-blob>
//
// A v2 obj_mergeini IGNORÁLJA a marker UTÁNI sorokat (mert nem
// `<type>.<name>=` formátum) → transzparens az engine-felé.

#include <memory>
#include <string>

#include "engine.h"
#ifdef obj
#undef obj
#endif

namespace editor {

class IComponentExtraSerializer {
public:
    virtual ~IComponentExtraSerializer() = default;
    // Return a serializer-defined string (e.g. JSON5 blob) of the extra fields.
    // Empty string → no extras to write (caller skips the marker).
    virtual std::string serialize(obj* o) = 0;
    // Parse the blob back into the object's extra fields. Called AFTER
    // obj_make has populated the v2-reflection fields.
    virtual void deserialize(obj* o, const std::string& blob) = 0;
};

class ExtraSerializerRegistry {
public:
    static ExtraSerializerRegistry& instance();

    // `objType` = the OBJTYPEDEF name (e.g. "MeshRenderer"). One serializer
    // per type; a second register-call replaces the previous.
    void registerSerializer(const char* objType,
                            std::unique_ptr<IComponentExtraSerializer> ser);

    // Returns nullptr if no serializer is registered for this type.
    IComponentExtraSerializer* lookup(const char* objType) const;

    // The marker string sentinel that separates the v2 ini-body from the
    // editor extras blob. Sole-purpose token: starts with 3 underscores +
    // an explicit `V1` version suffix, so collisions with user-data are
    // vanishingly unlikely. If we ever need to evolve the format, bump to
    // `___EDITOR_EXTRAS_V2___` and let loaders handle both.
    static constexpr const char* kMarker = "___EDITOR_EXTRAS_V1___";

private:
    ExtraSerializerRegistry() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    // We use a forward-declared Impl so this header doesn't need <unordered_map>.
    // The accessor below lazily initializes it on first use.
    void ensureImpl();
};

}  // namespace editor
