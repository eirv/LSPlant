module;

#include <tuple>
#include <unordered_map>

#include "logging.hpp"

#ifdef LSPLANT_USE_MODULES
export module lsplant:jit;

import :common;
import :art_method;
import :thread;
#endif

namespace lsplant::art::jit {
enum class CompilationKind {
    kOsr [[maybe_unused]],
    kBaseline [[maybe_unused]],
    kOptimized,
};

export class Jit {
    inline static auto EnqueueOptimizedCompilation_ =
        "_ZN3art3jit3Jit27EnqueueOptimizedCompilationEPNS_9ArtMethodEPNS_6ThreadE"_sym.hook->*
        []<MemBackup auto backup>(Jit *thiz, ArtMethod *method, Thread *self) static -> void {
        if (auto target = IsBackup(method); target) [[unlikely]] {
            LOGD("Propagate enqueue compilation: %p -> %p", method, target);
            method = target;
        }
        return backup(thiz, method, self);
    };

    inline static auto AddCompileTask_ =
        "_ZN3art3jit3Jit14AddCompileTaskEPNS_6ThreadEPNS_9ArtMethodENS_15CompilationKindEb"_sym.hook
            ->*[]<MemBackup auto backup>(Jit *thiz, Thread *self, ArtMethod *method,
                                         CompilationKind compilation_kind,
                                         bool precompile) static -> void {
        if (compilation_kind == CompilationKind::kOptimized && !precompile) {
            if (auto b = IsHooked(method); b) [[unlikely]] {
                LOGD("Propagate compile task: %p -> %p", method, b);
                method = b;
            }
        }
        return backup(thiz, self, method, compilation_kind, precompile);
    };

    inline static auto MapBootImageMethods_ = "_ZN3art3jit3Jit19MapBootImageMethodsEv"_sym.hook->*
                                              []<MemBackup auto backup>(Jit *thiz) static -> void {
        LOGD("Jit::MapBootImageMethods is started");

        std::unordered_map<ArtMethod *, std::tuple<ArtMethod *, void *, void *>> methods;
        for (const auto &[method, original_entry_point] : deoptimized_methods_) {
            methods[method] = {nullptr, method->GetData(), method->GetEntryPoint()};
            method->SetEntryPoint(original_entry_point);
        }
        for (const auto &p : hooked_methods_) {
            auto method = p.first;
            auto backup_method = p.second.backup;
            methods[method] = {backup_method, method->GetData(), method->GetEntryPoint()};
            method->SetData(backup_method->GetData());
            method->SetEntryPoint(backup_method->GetEntryPoint());
        }

        backup(thiz);

        for (const auto &it : methods) {
            auto method = it.first;
            auto [backup_method, data, entry_point] = it.second;

            if (!backup_method) {  // deoptimized
                if (method->GetEntryPoint() != entry_point) {
                    LOGD("Update entry point for deoptimized %s", method->PrettyMethod().c_str());
                    deoptimized_methods_[method] = method->GetEntryPoint();
                    method->SetEntryPoint(entry_point);
                }
            } else if (method->GetData() == backup_method->GetData() &&
                       method->GetEntryPoint() == backup_method->GetEntryPoint()) {
                method->SetData(data);
                method->SetEntryPoint(entry_point);
            } else {
                LOGD("Update entry point for hooked %s", method->PrettyMethod().c_str());
                backup_method->SetData(method->GetData());
                backup_method->SetEntryPoint(method->GetEntryPoint());
                method->SetEntryPoint(entry_point);  // trampoline
            }
        }
    };

public:
    static bool Init(const HookHandler &handler) {
        auto sdk_int = GetAndroidApiLevel();

        if (sdk_int <= __ANDROID_API_U__) [[likely]] {
            handler(EnqueueOptimizedCompilation_);
            handler(AddCompileTask_);
        }
        if (sdk_int >= __ANDROID_API_R__) [[likely]] {
            handler(MapBootImageMethods_);
        }
        return true;
    }
};
}  // namespace lsplant::art::jit
