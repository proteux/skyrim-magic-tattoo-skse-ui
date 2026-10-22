#include "game/Papyrus.h"

namespace mtfui::game {
    RE::BSScript::Internal::VirtualMachine* VM() {
        return RE::BSScript::Internal::VirtualMachine::GetSingleton();
    }

    RE::VMHandle HandleFor(RE::TESForm* form) {
        auto* vm = VM();
        if (!vm || !form) {
            return 0;
        }
        auto* policy = vm->GetObjectHandlePolicy();
        if (!policy) {
            return 0;
        }
        const auto handle = policy->GetHandleForObject(form->GetFormType(), form);
        return handle == policy->EmptyHandle() ? 0 : handle;
    }

    RE::BSTSmartPointer<RE::BSScript::Object> ScriptObject(RE::TESForm* form, const char* className) {
        RE::BSTSmartPointer<RE::BSScript::Object> obj;
        auto* vm = VM();
        const auto handle = HandleFor(form);
        if (vm && handle != 0) {
            vm->FindBoundObject(handle, className, obj);
        }
        return obj;
    }
}
