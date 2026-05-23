// STL FIRST.
#include <algorithm>
#include <vector>

#include "panel_registry.h"

namespace editor {

namespace {
std::vector<PanelDescriptor>& storage() {
    static std::vector<PanelDescriptor> v;
    return v;
}
}  // namespace

void PanelRegistry::add(PanelDescriptor d) {
    auto& v = storage();
    v.push_back(std::move(d));
    std::stable_sort(v.begin(), v.end(),
                     [](const PanelDescriptor& a, const PanelDescriptor& b) {
                         return a.order < b.order;
                     });
}

const std::vector<PanelDescriptor>& PanelRegistry::all() {
    return storage();
}

}  // namespace editor
