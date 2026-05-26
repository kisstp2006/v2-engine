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

    // Per-selection undo/redo support. The Inspector's `◀ ▶` arrows ask the
    // CommandStack to scan from the top down and find the most recent
    // command that touches the currently-selected node OR asset.
    // Default = unscoped (won't be found by per-target lookup).
    virtual bool affectsObject(obj* /*o*/) const { return false; }
    virtual bool affectsAsset(const std::string& /*absPath*/) const { return false; }
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
    bool affectsObject(obj* o) const override { return o && o == target_; }

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
    bool affectsObject(obj* o) const override { return o && (o == node_ || o == parent_); }

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
    bool affectsObject(obj* o) const override { return o && (o == node_ || o == parent_); }

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
    bool affectsObject(obj* o) const override {
        return o && (o == node_ || o == oldParent_ || o == newParent_);
    }

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
    bool affectsObject(obj* o) const override {
        if (!o) return false;
        for (obj* t : targets_) if (t == o) return true;
        return false;
    }

    // snapshotAll(nodes) — obj_saveini for every node.
    static std::vector<std::string> snapshotAll(const std::vector<obj*>& nodes);

private:
    std::vector<obj*>        targets_;
    std::vector<std::string> befores_;
    std::vector<std::string> afters_;
    std::string              name_;
};

// Asset-state command — snapshot-based diff of a file's CONTENTS. Used for
// `.mat.json5` Save (asset_preview.cpp) so material edits become undoable.
// undo / redo writes `before_` / `after_` back to disk; the editor's asset
// caches mtime-poll and reload on next access (no explicit invalidation).
class AssetStateCommand : public Command {
public:
    AssetStateCommand(std::string absPath, std::string before, std::string after,
                      std::string name);
    void undo() override;
    void redo() override;
    const char* name() const override { return name_.c_str(); }
    bool affectsAsset(const std::string& p) const override { return p == absPath_; }

private:
    void writeContents_(const std::string& contents);

    std::string absPath_;
    std::string before_;
    std::string after_;
    std::string name_;
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

    // Per-target undo/redo — scan from the TOP of the corresponding stack,
    // find the most recent command that touches the given target, pop it
    // out of the stack (rest of the stack is preserved in order), and run
    // its undo/redo. Returns true on action.
    //
    // Note: this can drift the per-target state away from the global
    // timeline (a later command on a different target still sits in undo_
    // after we pull this one out). That's intentional — it's a "scrub
    // back through THIS object's recent changes" feature, not a global
    // history navigator. Global Ctrl+Z still walks the full stack in order.
    bool canUndoForObject(obj* o) const;
    bool undoForObject(obj* o);
    bool canRedoForObject(obj* o) const;
    bool redoForObject(obj* o);

    bool canUndoForAsset(const std::string& absPath) const;
    bool undoForAsset(const std::string& absPath);
    bool canRedoForAsset(const std::string& absPath) const;
    bool redoForAsset(const std::string& absPath);

private:
    std::vector<std::unique_ptr<Command>> undo_;
    std::vector<std::unique_ptr<Command>> redo_;
    EventBus*                             bus_ = nullptr;
};

}  // namespace editor
