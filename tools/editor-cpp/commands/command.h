#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

namespace editor {

// Abstract base — minden mutáció (gizmo drag, Inspector edit, add/delete node)
// ezen át megy a CommandStack-be.
class Command {
public:
    virtual ~Command() = default;
    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual const char* name() const = 0;
};

// Snapshot-alapú állapot-command. A `target` obj-jellegű node teljes
// reflektált állapota a `before` és `after` INI-string-ben. Undo = before,
// redo = after — mindkettő obj_mergeini-vel rekonstruál.
class ObjectStateCommand : public Command {
public:
    ObjectStateCommand(obj* target, std::string before, std::string after,
                       std::string name);
    void undo() override;
    void redo() override;
    const char* name() const override { return name_.c_str(); }

    static std::string snapshot(obj* o);  // helper

private:
    obj* target_;
    std::string before_;
    std::string after_;
    std::string name_;
};

// Add-node command (Phase 3d). A caller a node-ot már attach-elte a
// parent-hez (a createXxx factory belül `obj_attach`-ol). A command az
// undo/redo-hoz tárolja a pointereket — undo = detach, redo = re-attach.
//
// Dtor: ha a node undone-állapotban van (detach-elve, és a stack-ből kiesik),
// `obj_free`-zi a memóriaszivárgás megelőzéseként.
class AddNodeCommand : public Command {
public:
    AddNodeCommand(obj* parent, obj* node, std::string name = "Add");
    ~AddNodeCommand() override;
    void undo() override;
    void redo() override;
    const char* name() const override { return name_.c_str(); }

private:
    obj*        parent_;
    obj*        node_;
    std::string name_;
};

// Delete-node command (Phase 3d). A caller a node-ot már detach-elte (a
// Delete-handler `obj_detach`-ol mielőtt push-olja a command-ot). Az undo
// vissza-attach-eli a régi parent-re.
//
// Dtor: ha a node detach-elt állapotban van (redo-state, a stack-ből kiesik),
// `obj_free`-zi.
class DeleteNodeCommand : public Command {
public:
    DeleteNodeCommand(obj* parent, obj* node, std::string name = "Delete");
    ~DeleteNodeCommand() override;
    void undo() override;
    void redo() override;
    const char* name() const override { return name_.c_str(); }

private:
    obj*        parent_;
    obj*        node_;
    std::string name_;
};

// Reparent-command (Phase 3a). A node-ot oldParent-ből newParent-be áthelyezi.
// A caller már megcsinálta a reparent-et (obj_attach(newParent, node)) — a
// command csak az undo/redo-hoz tárolja a pointer-eket.
//
// Megjegyzés: a motor `obj_attach(p, c)` belül `obj_detach(c)`-t hív, tehát
// nem kell külön detach az áthelyezéshez.
class ReparentCommand : public Command {
public:
    ReparentCommand(obj* node, obj* oldParent, obj* newParent,
                    std::string name = "Reparent");
    void undo() override;
    void redo() override;
    const char* name() const override { return name_.c_str(); }

private:
    obj*        node_;
    obj*        oldParent_;
    obj*        newParent_;
    std::string name_;
};

// Multi-target snapshot-command (Phase 2c). Pár (node + before + after)
// vektorokkal — Inspector multi-edit és multi-gizmo-drag undo-támogatás.
class MultiObjectStateCommand : public Command {
public:
    MultiObjectStateCommand(std::vector<obj*> targets,
                            std::vector<std::string> befores,
                            std::vector<std::string> afters,
                            std::string name);
    void undo() override;
    void redo() override;
    const char* name() const override { return name_.c_str(); }

    // snapshotAll(nodes) — minden node-ra obj_saveini.
    static std::vector<std::string> snapshotAll(const std::vector<obj*>& nodes);

private:
    std::vector<obj*>        targets_;
    std::vector<std::string> befores_;
    std::vector<std::string> afters_;
    std::string              name_;
};

class EventBus;

// Undo/Redo stack. Max 256 lépés, LRU evict. Új mutáció törli a redo stack-et.
class CommandStack {
public:
    static constexpr size_t kMaxDepth = 256;

    void setBus(EventBus* bus) { bus_ = bus; }

    // Egyetlen command beadása (nem transaction).
    void execute(std::unique_ptr<Command> cmd);

    void undo();
    void redo();

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    void clear();

private:
    std::vector<std::unique_ptr<Command>> undo_;
    std::vector<std::unique_ptr<Command>> redo_;
    EventBus*                             bus_ = nullptr;
};

}  // namespace editor
