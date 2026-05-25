// STL FIRST (engine `obj`/`is` macro-clash).
#include <memory>
#include <string>
#include <unordered_map>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "extra_serializer.h"

namespace editor {

struct ExtraSerializerRegistry::Impl {
    std::unordered_map<std::string, std::unique_ptr<IComponentExtraSerializer>> map;
};

ExtraSerializerRegistry& ExtraSerializerRegistry::instance() {
    static ExtraSerializerRegistry s;
    s.ensureImpl();
    return s;
}

void ExtraSerializerRegistry::ensureImpl() {
    if (!impl_) impl_.reset(new Impl());
}

void ExtraSerializerRegistry::registerSerializer(
        const char* objType,
        std::unique_ptr<IComponentExtraSerializer> ser) {
    if (!objType || !ser) return;
    ensureImpl();
    impl_->map[std::string(objType)] = std::move(ser);
}

IComponentExtraSerializer* ExtraSerializerRegistry::lookup(
        const char* objType) const {
    if (!objType || !impl_) return nullptr;
    auto it = impl_->map.find(std::string(objType));
    return it == impl_->map.end() ? nullptr : it->second.get();
}

}  // namespace editor
