// STL FIRST.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "command.h"
#include "../core/event_bus.h"
#include "../core/events.h"

namespace editor {

namespace {
// Read whole file → string. Empty string on error (treated as "no snapshot",
// undo/redo becomes a no-op rather than truncating the file to 0 bytes).
std::string readFileWhole(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
bool writeFileWhole(const std::string& path, const std::string& contents) {
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path(), ec);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return false;
    f.write(contents.data(), (std::streamsize)contents.size());
    return f.good();
}
}  // namespace

// ---- ObjectStateCommand ---------------------------------------------------

std::string ObjectStateCommand::snapshot(obj* o) {
    if (!o) return {};
    char* ini = obj_saveini(o);
    return std::string(ini ? ini : "");
}

ObjectStateCommand::ObjectStateCommand(obj* target,
                                       std::string before,
                                       std::string after,
                                       std::string name)
    : target_(target),
      before_(std::move(before)),
      after_(std::move(after)),
      name_(std::move(name)) {}

void ObjectStateCommand::undo() {
    if (!target_ || before_.empty()) return;
    obj_mergeini(target_, before_.c_str());
}

void ObjectStateCommand::redo() {
    if (!target_ || after_.empty()) return;
    obj_mergeini(target_, after_.c_str());
}

// ---- AddNodeCommand -------------------------------------------------------

AddNodeCommand::AddNodeCommand(obj* parent, obj* node, std::string name)
    : parent_(parent), node_(node), name_(std::move(name)) {}

AddNodeCommand::~AddNodeCommand() {
    // If in an undone state (parent NULL = detached), and it falls off the
    // stack → free, so memory doesn't leak.
    if (node_ && !obj_parent(node_)) {
        obj_free(node_);
    }
}

void AddNodeCommand::undo() {
    if (node_) obj_detach(node_);
}

void AddNodeCommand::redo() {
    if (node_ && parent_) obj_attach(parent_, node_);
}

// ---- DeleteNodeCommand ----------------------------------------------------

DeleteNodeCommand::DeleteNodeCommand(obj* parent, obj* node, std::string name)
    : parent_(parent), node_(node), name_(std::move(name)) {}

DeleteNodeCommand::~DeleteNodeCommand() {
    // If in a redo state (parent NULL = detached), and it falls off the stack
    // → free. If in an undo state (re-attached), the parent holds it → do NOT free.
    if (node_ && !obj_parent(node_)) {
        obj_free(node_);
    }
}

void DeleteNodeCommand::undo() {
    if (node_ && parent_) obj_attach(parent_, node_);
}

void DeleteNodeCommand::redo() {
    if (node_) obj_detach(node_);
}

// ---- ReparentCommand ------------------------------------------------------

ReparentCommand::ReparentCommand(obj* node, obj* oldParent, obj* newParent,
                                 std::string name)
    : node_(node),
      oldParent_(oldParent),
      newParent_(newParent),
      name_(std::move(name)) {}

void ReparentCommand::undo() {
    if (!node_) return;
    if (oldParent_) {
        obj_attach(oldParent_, node_);
    } else {
        // It used to be root — detach (now it's under newParent).
        obj_detach(node_);
    }
}

void ReparentCommand::redo() {
    if (!node_) return;
    if (newParent_) {
        obj_attach(newParent_, node_);
    } else {
        obj_detach(node_);
    }
}

// ---- RenameNodeCommand ---------------------------------------------------

RenameNodeCommand::RenameNodeCommand(obj* node, std::string oldName,
                                     std::string newName, std::string label)
    : node_(node),
      oldName_(std::move(oldName)),
      newName_(std::move(newName)),
      label_(std::move(label)) {}

void RenameNodeCommand::undo() {
    if (!node_) return;
    obj_setname(node_, oldName_.c_str());
}

void RenameNodeCommand::redo() {
    if (!node_) return;
    obj_setname(node_, newName_.c_str());
}

// ---- MultiObjectStateCommand ---------------------------------------------

MultiObjectStateCommand::MultiObjectStateCommand(
    std::vector<obj*> targets,
    std::vector<std::string> befores,
    std::vector<std::string> afters,
    std::string name)
    : targets_(std::move(targets)),
      befores_(std::move(befores)),
      afters_(std::move(afters)),
      name_(std::move(name)) {}

void MultiObjectStateCommand::undo() {
    for (size_t i = 0; i < targets_.size(); ++i) {
        if (targets_[i] && i < befores_.size() && !befores_[i].empty()) {
            obj_mergeini(targets_[i], befores_[i].c_str());
        }
    }
}

void MultiObjectStateCommand::redo() {
    for (size_t i = 0; i < targets_.size(); ++i) {
        if (targets_[i] && i < afters_.size() && !afters_[i].empty()) {
            obj_mergeini(targets_[i], afters_[i].c_str());
        }
    }
}

std::vector<std::string> MultiObjectStateCommand::snapshotAll(
        const std::vector<obj*>& nodes) {
    std::vector<std::string> out;
    out.reserve(nodes.size());
    for (obj* n : nodes) out.push_back(ObjectStateCommand::snapshot(n));
    return out;
}

// ---- CommandStack ---------------------------------------------------------

void CommandStack::execute(std::unique_ptr<Command> cmd) {
    if (!cmd) return;
    // A new mutation clears the redo stack.
    redo_.clear();
    undo_.push_back(std::move(cmd));
    while (undo_.size() > kMaxDepth) {
        undo_.erase(undo_.begin());
    }
    if (bus_) bus_->emit(kEvtSceneDirty, true);
}

void CommandStack::undo() {
    if (undo_.empty()) return;
    auto cmd = std::move(undo_.back());
    undo_.pop_back();
    cmd->undo();
    redo_.push_back(std::move(cmd));
}

void CommandStack::redo() {
    if (redo_.empty()) return;
    auto cmd = std::move(redo_.back());
    redo_.pop_back();
    cmd->redo();
    undo_.push_back(std::move(cmd));
}

void CommandStack::clear() {
    undo_.clear();
    redo_.clear();
}

// ---- AssetStateCommand ----------------------------------------------------

AssetStateCommand::AssetStateCommand(std::string absPath,
                                     std::string before,
                                     std::string after,
                                     std::string name)
    : absPath_(std::move(absPath)),
      before_(std::move(before)),
      after_(std::move(after)),
      name_(std::move(name)) {}

void AssetStateCommand::writeContents_(const std::string& contents) {
    if (absPath_.empty() || contents.empty()) return;
    writeFileWhole(absPath_, contents);
}

void AssetStateCommand::undo() { writeContents_(before_); }
void AssetStateCommand::redo() { writeContents_(after_);  }

// ---- CommandStack per-target lookup ---------------------------------------

bool CommandStack::canUndoForObject(obj* o) const {
    if (!o) return false;
    for (auto it = undo_.rbegin(); it != undo_.rend(); ++it) {
        if ((*it)->affectsObject(o)) return true;
    }
    return false;
}

bool CommandStack::undoForObject(obj* o) {
    if (!o) return false;
    for (size_t i = undo_.size(); i-- > 0; ) {
        if (undo_[i]->affectsObject(o)) {
            auto cmd = std::move(undo_[i]);
            undo_.erase(undo_.begin() + (std::ptrdiff_t)i);
            cmd->undo();
            redo_.push_back(std::move(cmd));
            if (bus_) bus_->emit(kEvtSceneDirty, true);
            return true;
        }
    }
    return false;
}

bool CommandStack::canRedoForObject(obj* o) const {
    if (!o) return false;
    for (auto it = redo_.rbegin(); it != redo_.rend(); ++it) {
        if ((*it)->affectsObject(o)) return true;
    }
    return false;
}

bool CommandStack::redoForObject(obj* o) {
    if (!o) return false;
    for (size_t i = redo_.size(); i-- > 0; ) {
        if (redo_[i]->affectsObject(o)) {
            auto cmd = std::move(redo_[i]);
            redo_.erase(redo_.begin() + (std::ptrdiff_t)i);
            cmd->redo();
            undo_.push_back(std::move(cmd));
            if (bus_) bus_->emit(kEvtSceneDirty, true);
            return true;
        }
    }
    return false;
}

bool CommandStack::canUndoForAsset(const std::string& p) const {
    if (p.empty()) return false;
    for (auto it = undo_.rbegin(); it != undo_.rend(); ++it) {
        if ((*it)->affectsAsset(p)) return true;
    }
    return false;
}

bool CommandStack::undoForAsset(const std::string& p) {
    if (p.empty()) return false;
    for (size_t i = undo_.size(); i-- > 0; ) {
        if (undo_[i]->affectsAsset(p)) {
            auto cmd = std::move(undo_[i]);
            undo_.erase(undo_.begin() + (std::ptrdiff_t)i);
            cmd->undo();
            redo_.push_back(std::move(cmd));
            if (bus_) bus_->emit(kEvtSceneDirty, true);
            return true;
        }
    }
    return false;
}

bool CommandStack::canRedoForAsset(const std::string& p) const {
    if (p.empty()) return false;
    for (auto it = redo_.rbegin(); it != redo_.rend(); ++it) {
        if ((*it)->affectsAsset(p)) return true;
    }
    return false;
}

bool CommandStack::redoForAsset(const std::string& p) {
    if (p.empty()) return false;
    for (size_t i = redo_.size(); i-- > 0; ) {
        if (redo_[i]->affectsAsset(p)) {
            auto cmd = std::move(redo_[i]);
            redo_.erase(redo_.begin() + (std::ptrdiff_t)i);
            cmd->redo();
            undo_.push_back(std::move(cmd));
            if (bus_) bus_->emit(kEvtSceneDirty, true);
            return true;
        }
    }
    return false;
}

}  // namespace editor
