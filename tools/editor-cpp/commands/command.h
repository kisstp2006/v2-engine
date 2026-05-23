#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

namespace editor {

// Abstract base — every mutation (gizmo drag, Inspector edit, add/delete node)
// goes through this into the CommandStack.
class Command {
public:
    virtual ~Command() = default;
    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual const char* name() const = 0;
};

// Snapshot-based state-command. The complete reflected state of the `target`
// obj-like node lives in the `before` and `after` INI-strings. Undo = before,
// redo = after — both reconstruct via obj_mergeini.
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

// Add-node command (Phase 3d). The caller has already attached the node to
// the parent (the createXxx factory calls `obj_attach` internally). The
// command stores the pointers for undo/redo — undo = detach, redo = re-attach.
//
// Dtor: if the node is in an undone state (detached, and the stack drops it),
// it `obj_free`s it to prevent memory leaks.
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

// Delete-node command (Phase 3d). The caller has already detached the node
// (the Delete-handler calls `obj_detach` before pushing the command). Undo
// re-attaches it to the old parent.
//
// Dtor: if the node is in a detached state (redo-state, dropped from the
// stack), it `obj_free`s it.
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

// Reparent-command (Phase 3a). Moves the node from oldParent to newParent.
// The caller has already done the reparent (obj_attach(newParent, node)) —
// the command only stores the pointers for undo/redo.
//
// Note: the engine's `obj_attach(p, c)` calls `obj_detach(c)` internally,
// so a separate detach is not needed for the move.
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

// Multi-target snapshot-command (Phase 2c). With pair (node + before + after)
// vectors — Inspector multi-edit and multi-gizmo-drag undo support.
class MultiObjectStateCommand : public Command {
public:
    MultiObjectStateCommand(std::vector<obj*> targets,
                            std::vector<std::string> befores,
                            std::vector<std::string> afters,
                            std::string name);
    void undo() override;
    void redo() override;
    const char* name() const override { return name_.c_str(); }

    // snapshotAll(nodes) — obj_saveini for every node.
    static std::vector<std::string> snapshotAll(const std::vector<obj*>& nodes);

private:
    std::vector<obj*>        targets_;
    std::vector<std::string> befores_;
    std::vector<std::string> afters_;
    std::string              name_;
};

class EventBus;

// Undo/Redo stack. Max 256 steps, LRU evict. A new mutation clears the redo stack.
class CommandStack {
public:
    static constexpr size_t kMaxDepth = 256;

    void setBus(EventBus* bus) { bus_ = bus; }

    // Push a single command (not a transaction).
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
