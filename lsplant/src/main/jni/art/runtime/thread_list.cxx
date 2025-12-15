module;

#ifdef LSPLANT_USE_MODULES
export module lsplant:thread_list;

import :common;
#endif

namespace lsplant::art::thread_list {

export class ScopedSuspendAll {
    inline static auto constructor_ =
        "_ZN3art16ScopedSuspendAllC2EPKcb"_sym.as<void (ScopedSuspendAll::*)(const char *, bool)>;

    inline static auto destructor_ =
        "_ZN3art16ScopedSuspendAllD2Ev"_sym.as<void (ScopedSuspendAll::*)()>;

    inline static auto SuspendVM_ = "_ZN3art3Dbg9SuspendVMEv"_sym.as<void()>;
    inline static auto ResumeVM_ = "_ZN3art3Dbg8ResumeVMEv"_sym.as<void()>;

public:
    ScopedSuspendAll(const char *cause, bool long_suspend) {
        if (constructor_) [[likely]] {
            constructor_(this, cause, long_suspend);
        } else if (SuspendVM_) {
            SuspendVM_();
        }
    }

    ~ScopedSuspendAll() {
        if (destructor_) [[likely]] {
            destructor_(this);
        } else if (ResumeVM_) {
            ResumeVM_();
        }
    }

    static bool Init(const HookHandler &handler) {
        return handler.all(constructor_, destructor_) || handler.all(SuspendVM_, ResumeVM_);
    }
};

}  // namespace lsplant::art::thread_list
