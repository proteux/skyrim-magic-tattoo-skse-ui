#pragma once

namespace mtfui::game {
    using VmResult = std::function<void(const RE::BSScript::Variable&)>;

    // Runs on the VM thread when a dispatched call returns.
    class CallbackFunctor final : public RE::BSScript::IStackCallbackFunctor {
    public:
        explicit CallbackFunctor(VmResult cb) : cb_(std::move(cb)) {}
        void operator()(RE::BSScript::Variable result) override {
            if (cb_) {
                cb_(result);
            }
        }
        void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

    private:
        VmResult cb_;
    };

    RE::BSScript::Internal::VirtualMachine* VM();
    RE::VMHandle HandleFor(RE::TESForm* form);
    RE::BSTSmartPointer<RE::BSScript::Object> ScriptObject(RE::TESForm* form, const char* className);

    // Global (static) Papyrus function, e.g. JsonUtil.Unload.
    template <class... Args>
    bool CallStatic(const char* className, const char* fn, VmResult cb, Args&&... args) {
        auto* vm = VM();
        if (!vm) {
            return false;
        }
        auto* fargs = RE::MakeFunctionArguments(std::decay_t<Args>(std::forward<Args>(args))...);
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> functor(new CallbackFunctor(std::move(cb)));
        return vm->DispatchStaticCall(className, fn, fargs, functor);
    }

    // Method on the script of className attached to form.
    template <class... Args>
    bool CallMethod(RE::TESForm* form, const char* className, const char* fn, VmResult cb, Args&&... args) {
        auto* vm = VM();
        const auto handle = HandleFor(form);
        if (!vm || handle == 0) {
            return false;
        }
        auto* fargs = RE::MakeFunctionArguments(std::decay_t<Args>(std::forward<Args>(args))...);
        RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> functor(new CallbackFunctor(std::move(cb)));
        return vm->DispatchMethodCall(handle, className, fn, fargs, functor);
    }
}
