#pragma once

#include <functional>
#include <memory>
#include <vector>

namespace editor {

class Panel;

// Egy regisztrált panel leírója — sorrend + factory.
struct PanelDescriptor {
    int order;
    std::function<std::unique_ptr<Panel>()> factory;
};

// Statikus panel registry. A panel `.cpp` fájlok a REGISTER_PANEL makróval
// regisztrálják magukat — még `main()` előtt, static initializer-ben.
class PanelRegistry {
public:
    static void add(PanelDescriptor d);
    static const std::vector<PanelDescriptor>& all();
};

}  // namespace editor

// Új panel hozzáadása: a panel TYPE class-od `.cpp` fájljának végén,
// `namespace editor { ... }` blokkon BELÜL:
//
//     REGISTER_PANEL(HierarchyPanel, 200)
//
// ORDER = a panel megjelenési sorrendje (kisebb = előbb). A registry
// stabil-sortolja az add-ok után.
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
