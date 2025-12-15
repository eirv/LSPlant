module;

#include <dlfcn.h>
#include <parallel_hashmap/phmap.h>

#include <array>
#include <cstddef>
#include <tuple>

#include "logging.hpp"

#ifdef LSPLANT_USE_MODULES
export module lsplant:class_linker;

import :common;
import :clazz;
import :art_method;
import :handle;
import :instrumentation;
import :runtime;
import :thread;
#endif

namespace lsplant::art {

class NativeMethodEntryPointUpdater {
public:
    NativeMethodEntryPointUpdater(JNIEnv *env, jclass clazz, const JNINativeMethod *native_methods,
                                  jint native_method_count) {
        if (!env || !clazz || !native_methods || native_method_count <= 0) [[unlikely]] {
            return;
        }

        auto is_static = true;
        auto getter = std::array{env->functions->GetMethodID, env->functions->GetStaticMethodID};

        for (jint i = 0; native_method_count > i; ++i) {
            auto &native_method = native_methods[i];
            if (!native_method.name || !native_method.signature || !native_method.fnPtr)
                [[unlikely]] {
                continue;
            }

            auto method_id =
                getter[is_static](env, clazz, native_method.name, native_method.signature);
            if (!method_id) [[unlikely]] {
                is_static = !is_static;
                method_id =
                    getter[is_static](env, clazz, native_method.name, native_method.signature);
                if (!method_id) [[unlikely]] {
                    continue;
                }
            }

            auto method = ArtMethod::FromJMethodID(env, clazz, method_id, is_static);
            auto backup = IsHooked(method);
            if (!backup || !backup->IsNative()) continue;

            methods_[method] = {backup, method->GetData(), method->GetEntryPoint()};
        }
    }

    NativeMethodEntryPointUpdater(JNIEnv *env, jclass clazz) {
        if (!env || !clazz) [[unlikely]] {
            return;
        }

        mirror::Class::VisitMethods(env, clazz, [this](auto method) {
            if (!method->IsNative()) return false;

            auto backup = IsHooked(method);
            if (!backup || !backup->IsNative()) return false;

            methods_[method] = {backup, method->GetData(), method->GetEntryPoint()};
            return false;
        });
    }

    ~NativeMethodEntryPointUpdater() {
        for (const auto &p : methods_) {
            auto method = p.first;
            auto [backup, data, entry_point] = p.second;

            if (method->GetData() == data && method->GetEntryPoint() == entry_point) continue;

            backup->SetData(method->GetData());
            backup->SetEntryPoint(method->GetEntryPoint());

            method->SetData(data);
            method->SetEntryPoint(entry_point);
        }
    }

private:
    phmap::flat_hash_map<ArtMethod *, std::tuple<ArtMethod *, void *, void *>> methods_{};
};

export class ClassLinker {
private:
    inline static auto SetEntryPointsToInterpreter_ =
        "_ZNK3art11ClassLinker27SetEntryPointsToInterpreterEPNS_9ArtMethodE"_sym
            .as<void (ClassLinker::*)(ArtMethod *)>;

    inline static auto ShouldUseInterpreterEntrypoint_ =
        "_ZN3art11ClassLinker30ShouldUseInterpreterEntrypointEPNS_9ArtMethodEPKv"_sym.hook->*
        []<Backup auto backup>(ArtMethod *art_method, const void *quick_code) static -> bool {
        if (quick_code != nullptr && IsHooked(art_method)) [[unlikely]] {
            return false;
        }
        return backup(art_method, quick_code);
    };

    inline static auto art_quick_to_interpreter_bridge_ =
        "art_quick_to_interpreter_bridge"_sym.as<void(void *)>;

    inline static auto GetOptimizedCodeFor_ =
        "_ZN3art15instrumentationL19GetOptimizedCodeForEPNS_9ArtMethodE"_sym
            .as<void *(ArtMethod *)>;

    static art::ArtMethod *MayGetBackup(art::ArtMethod *method) {
        if (auto backup = IsHooked(method); backup) [[unlikely]] {
            method = backup;
            LOGV("propagate native method: %s", method->PrettyMethod(true).data());
        }
        return method;
    }

    inline static auto RegisterNativeThread_ =
        "_ZN3art6mirror9ArtMethod14RegisterNativeEPNS_6ThreadEPKvb"_sym.hook->*
        []<MemBackup auto backup>(ArtMethod *method, Thread *thread, const void *native_method,
                                  bool is_fast) static -> void {
        return backup(MayGetBackup(method), thread, native_method, is_fast);
    };

    inline static auto UnregisterNativeThread_ =
        "_ZN3art6mirror9ArtMethod16UnregisterNativeEPNS_6ThreadE"_sym.hook->*
        []<MemBackup auto backup>(ArtMethod *method, Thread *thread) static -> void {
        return backup(MayGetBackup(method), thread);
    };

    inline static auto RegisterNativeFast_ =
        "_ZN3art9ArtMethod14RegisterNativeEPKvb"_sym.hook->*
        []<MemBackup auto backup>(ArtMethod *method, const void *native_method,
                                  bool is_fast) static -> void {
        return backup(MayGetBackup(method), native_method, is_fast);
    };

    inline static auto UnregisterNativeFast_ =
        "_ZN3art9ArtMethod16UnregisterNativeEv"_sym.hook->*
        []<MemBackup auto backup>(ArtMethod *method) static -> void {
        return backup(MayGetBackup(method));
    };

    inline static auto RegisterNative_ =
        "_ZN3art9ArtMethod14RegisterNativeEPKv"_sym.hook->*
        []<MemBackup auto backup>(ArtMethod *method,
                                  const void *native_method) static -> const void * {
        return backup(MayGetBackup(method), native_method);
    };

    inline static auto UnregisterNative_ =
        "_ZN3art9ArtMethod16UnregisterNativeEv"_sym.hook->*
        []<MemBackup auto backup>(ArtMethod *method) static -> const void * {
        return backup(MayGetBackup(method));
    };

    inline static auto RegisterNativeClassLinker_ =
        "_ZN3art11ClassLinker14RegisterNativeEPNS_6ThreadEPNS_9ArtMethodEPKv"_sym.hook->*
        []<MemBackup auto backup>(ClassLinker *thiz, Thread *self, ArtMethod *method,
                                  const void *native_method) static -> const void * {
        return backup(thiz, self, MayGetBackup(method), native_method);
    };

    inline static auto UnregisterNativeClassLinker_ =
        "_ZN3art11ClassLinker16UnregisterNativeEPNS_6ThreadEPNS_9ArtMethodE"_sym.hook->*
        []<MemBackup auto backup>(ClassLinker *thiz, Thread *self,
                                  ArtMethod *method) static -> const void * {
        return backup(thiz, self, MayGetBackup(method));
    };

    inline static auto RegisterNativesJNIEnv_ =
        ""_sym.hook->*[]<Backup auto backup>(JNIEnv *env, jclass clazz,
                                             const JNINativeMethod *native_methods,
                                             jint native_method_count) static -> jint {
        NativeMethodEntryPointUpdater updater{env, clazz, native_methods, native_method_count};
        return backup(env, clazz, native_methods, native_method_count);
    };

    inline static auto UnregisterNativesJNIEnv_ =
        ""_sym.hook->*[]<Backup auto backup>(JNIEnv *env, jclass clazz) static -> jint {
        NativeMethodEntryPointUpdater updater{env, clazz};
        return backup(env, clazz);
    };

    static void RestoreBackup(const dex::ClassDef *class_def, art::Thread *self) {
        auto methods = mirror::Class::PopBackup(class_def, self);
        for (const auto &[art_method, old_trampoline] : methods) {
            auto new_trampoline = art_method->GetEntryPoint();
            art_method->SetEntryPoint(old_trampoline);
            auto deoptimized = IsDeoptimized(art_method);
            auto backup_method = IsHooked(art_method);
            if (backup_method) {
                // If deoptimized, the backup entrypoint should be already set to interpreter
                if (!deoptimized && new_trampoline != old_trampoline) [[unlikely]] {
                    LOGV("Propagate entrypoint for orig %p backup %p", art_method, backup_method);
                    backup_method->SetEntryPoint(new_trampoline);
                }
            } else if (deoptimized) {
                if (new_trampoline != &art_quick_to_interpreter_bridge_ &&
                    !art_method->IsNative()) {
                    LOGV("Re-deoptimize for %p", art_method);
                    SetEntryPointsToInterpreter(art_method);
                }
            }
        }
    }

    inline static auto FixupStaticTrampolines_ =
        "_ZN3art11ClassLinker22FixupStaticTrampolinesENS_6ObjPtrINS_6mirror5ClassEEE"_sym.hook->*
        []<MemBackup auto backup>(ClassLinker *thiz,
                                  ObjPtr<mirror::Class> mirror_class) static -> void {
        backup(thiz, mirror_class);
        RestoreBackup(mirror_class->GetClassDef(), nullptr);
    };

    inline static auto FixupStaticTrampolinesWithThread_ =
        "_ZN3art11ClassLinker22FixupStaticTrampolinesEPNS_6ThreadENS_6ObjPtrINS_6mirror5ClassEEE"_sym
            .hook
            ->*[]<MemBackup auto backup>(ClassLinker *thiz, Thread *self,
                                         ObjPtr<mirror::Class> mirror_class) static -> void {
        backup(thiz, self, mirror_class);
        RestoreBackup(mirror_class->GetClassDef(), self);
    };

    inline static auto FixupStaticTrampolinesRaw_ =
        "_ZN3art11ClassLinker22FixupStaticTrampolinesEPNS_6mirror5ClassE"_sym.hook->*
        []<MemBackup auto backup>(ClassLinker *thiz, mirror::Class *mirror_class) static -> void {
        backup(thiz, mirror_class);
        RestoreBackup(mirror_class->GetClassDef(), nullptr);
    };

    inline static auto AdjustThreadVisibilityCounter_ =
        ("_ZN3art11ClassLinker26VisiblyInitializedCallback29AdjustThreadVisibilityCounterEPNS_6ThreadEi"_sym |
         "_ZN3art11ClassLinker26VisiblyInitializedCallback29AdjustThreadVisibilityCounterEPNS_6ThreadEl"_sym)
            .hook
            ->*[]<MemBackup auto backup>(ClassLinker *thiz, Thread *self,
                                         ssize_t adjustment) static -> void {
        backup(thiz, self, adjustment);
        RestoreBackup(nullptr, self);
    };

    inline static auto MarkVisiblyInitialized_ =
        "_ZN3art11ClassLinker26VisiblyInitializedCallback22MarkVisiblyInitializedEPNS_6ThreadE"_sym
            .hook
            ->*[]<MemBackup auto backup>(ClassLinker *thiz, Thread *self) static -> void {
        backup(thiz, self);
        RestoreBackup(nullptr, self);
    };

public:
    static bool Init(JNIEnv *env, const HookHandler &handler) {
        int sdk_int = GetAndroidApiLevel();

        if (sdk_int >= __ANDROID_API_N__ && sdk_int < __ANDROID_API_T__) {
            handler(ShouldUseInterpreterEntrypoint_);
        }

        if (!handler.all(RegisterNativeClassLinker_, UnregisterNativeClassLinker_) &&
            !handler.all(RegisterNative_, UnregisterNative_) &&
            !handler.all(RegisterNativeFast_, UnregisterNativeFast_) &&
            !handler.all(RegisterNativeThread_, UnregisterNativeThread_) &&
            !handler.all(env->functions->RegisterNatives, RegisterNativesJNIEnv_,
                         env->functions->UnregisterNatives, UnregisterNativesJNIEnv_)) {
            LOGE("Failed to hook RegisterNative or UnregisterNative");
            return false;
        }

        auto may_update_entrypoint = !handler(FixupStaticTrampolinesWithThread_,
                                              FixupStaticTrampolines_, FixupStaticTrampolinesRaw_);

        if constexpr (!is_arch_v<Arch::kX86, Arch::kX64>) {
            // fixup static trampoline may have been inlined
            if (sdk_int >= __ANDROID_API_R__ &&
                handler(AdjustThreadVisibilityCounter_, MarkVisiblyInitialized_)) {
                may_update_entrypoint = false;
            }
        }

        if (may_update_entrypoint || Instrumentation::MayUpdateMethodsCode()) {
            LOGE(
                "Failed to hook FixupStaticTrampolines, AdjustThreadVisibilityCounter or MarkVisiblyInitialized");
            return false;
        }

        if (sdk_int < __ANDROID_API_T__ && handler(SetEntryPointsToInterpreter_)) [[unlikely]] {
            return true;
        }

        if (!handler(art_quick_to_interpreter_bridge_)) [[unlikely]] {
            if (sdk_int >= __ANDROID_API_T__ && handler(GetOptimizedCodeFor_, true)) [[likely]] {
                auto obj = JNI_FindClass(env, "java/lang/Object");
                auto dummy =
                    ArtMethod::FindMethod(env, obj.get(), "equals", "(Ljava/lang/Object;)Z")
                        ->Clone();
                JavaDebuggableGuard guard;
                // just in case
                dummy->SetNonNative();
                art_quick_to_interpreter_bridge_ = GetOptimizedCodeFor_(dummy.get());
            }
        }

        if (art_quick_to_interpreter_bridge_) [[likely]] {
            if constexpr (kDebugBuild) {
                [[maybe_unused]] auto [base, offset] = [] -> std::tuple<void *, size_t> {
                    Dl_info info{};
                    auto func_addr = reinterpret_cast<size_t>(&art_quick_to_interpreter_bridge_);
                    if (dladdr(reinterpret_cast<void *>(func_addr), &info) &&
                        reinterpret_cast<size_t>(info.dli_fbase) < func_addr) [[likely]] {
                        return {info.dli_fbase,
                                func_addr - reinterpret_cast<size_t>(info.dli_fbase)};
                    }
                    return {nullptr, func_addr};
                }();
                LOGD("art_quick_to_interpreter_bridge = %p+%#zx", base, offset);
            } else {
                LOGD("art_quick_to_interpreter_bridge = %p", &art_quick_to_interpreter_bridge_);
            }
        } else {
            LOGW("Can't set entry points to interpreter, deoptimize will not work.");
        }

        return true;
    }

    [[gnu::always_inline]] static bool CanSetEntryPointsToInterpreter() {
        return art_quick_to_interpreter_bridge_ || SetEntryPointsToInterpreter_;
    }

    [[gnu::always_inline]] static bool SetEntryPointsToInterpreter(ArtMethod *art_method) {
        if (art_quick_to_interpreter_bridge_) [[likely]] {
            LOGV("Deoptimize method %s from %p to %p", art_method->PrettyMethod(true).data(),
                 art_method->GetEntryPoint(), &art_quick_to_interpreter_bridge_);
            art_method->SetEntryPoint(reinterpret_cast<void *>(&art_quick_to_interpreter_bridge_));
            return true;
        }
        // Android 5~12
        if (SetEntryPointsToInterpreter_) [[likely]] {
            SetEntryPointsToInterpreter_(nullptr, art_method);
            return true;
        }
        return false;
    }
};
}  // namespace lsplant::art
