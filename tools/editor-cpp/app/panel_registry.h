#pragma once

#include <functional>
#include <memory>
#include <vector>

namespace editor {

class Panel;

// Descriptor of a registered panel — order + factory.
struct PanelDescriptor {
    int order;
    std::function<std::unique_ptr<Panel>()> factory;
};

// Static panel registry. Panel `.cpp` files register themselves with the
// REGISTER_PANEL macro — before `main()`, in a static initializer.
class PanelRegistry {
public:
    static void add(PanelDescriptor d);
    static const std::vector<PanelDescriptor>& all();
};

}  // namespace editor

// Adding a new panel: at the end of your panel TYPE class's `.cpp` file,
// INSIDE the `namespace editor { ... }` block:
//
//     REGISTER_PANEL(HierarchyPanel, 200)
//
// ORDER = the panel's display order (smaller = earlier). The registry
// stable-sorts after the adds.
#define REGISTER_PANEL(TYPE, ORDER)                                            \
    namespace {                                                                \
        const bool _##TYPE##_reg = (                                           \
            ::editor::PanelRegistry::add({                                     \
                (ORDER),                                                       \
                []() -> std::unique_ptr<::editor::Panel> {                     \
                    return std::make_unique<TYPE>();                           \
                }                                                              \
            }),                                                                \
            true);                                                             \
    }
