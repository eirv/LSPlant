module;

#include "logging.hpp"

#ifdef LSPLANT_USE_MODULES
export module lsplant:instrumentation;

import :common;
import :art_method;
#endif

namespace lsplant::art {

export class Instrumentation {
    static ArtMethod *MaybeUseBackupMethod(ArtMethod *art_method, const void *quick_code) {
        if (auto backup = IsHooked(art_method); backup && art_method->GetEntryPoint() != quick_code)
            [[unlikely]] {
            LOGD("Propagate update method code %p for hooked method %p to its backup", quick_code,
                 art_method);
            return backup;
        }
        return art_method;
    }

    inline static auto UpdateMethodsCodeToInterpreterEntryPoint_ =
        "_ZN3art15instrumentation15Instrumentation40UpdateMethodsCodeToInterpreterEntryPointEPNS_9ArtMethodE"_sym
            .hook
            ->*
        []<MemBackup auto backup>(Instrumentation *thiz, ArtMethod *art_method) static -> void {
        if (IsDeoptimized(art_method)) {
            LOGV("Skip update entrypoint on deoptimized method %s",
                 art_method->PrettyMethod(true).c_str());
            return;
        }
        backup(thiz, MaybeUseBackupMethod(art_method, nullptr));
    };

    inline static auto InitializeMethodsCode_ =
        "_ZN3art15instrumentation15Instrumentation21InitializeMethodsCodeEPNS_9ArtMethodEPKv"_sym
            .hook
            ->*[]<MemBackup auto backup>(Instrumentation *thiz, ArtMethod *art_method,
                                         const void *quick_code) static -> void {
        if (IsDeoptimized(art_method)) {
            LOGV("Skip update entrypoint on deoptimized method %s",
                 art_method->PrettyMethod(true).c_str());
            return;
        }
        backup(thiz, MaybeUseBackupMethod(art_method, quick_code), quick_code);
    };

    inline static auto ReinitializeMethodsCode_ =
        "_ZN3art15instrumentation15Instrumentation23ReinitializeMethodsCodeEPNS_9ArtMethodE"_sym
            .hook
            ->*
        []<MemBackup auto backup>(Instrumentation *thiz, ArtMethod *art_method) static -> void {
        if (IsDeoptimized(art_method)) {
            LOGV("Skip update entrypoint on deoptimized method %s",
                 art_method->PrettyMethod(true).c_str());
            return;
        }
        backup(thiz, MaybeUseBackupMethod(art_method, nullptr));
    };

    static void HandleMethodCodeUpdated(ArtMethod *method, void *old_code, const void *new_code) {
        auto deoptimized = IsDeoptimized(method);
        if (auto backup = IsHooked(method)) [[unlikely]] {
            method->SetEntryPoint(old_code);
            if (!deoptimized) {
                LOGD("Propagate update method code %p for hooked method %s to its backup", new_code,
                     method->PrettyMethod().c_str());
                backup->SetEntryPoint(const_cast<void *>(new_code));
            }
        } else if (deoptimized) [[unlikely]] {
            LOGD("Restore method code '%p -> %p' for deoptimized method %s", new_code, old_code,
                 method->PrettyMethod().c_str());
            method->SetEntryPoint(old_code);
        }
    }

    inline static auto UpdateMethodsCodeImpl_ =
        "_ZN3art15instrumentation15Instrumentation21UpdateMethodsCodeImplEPNS_9ArtMethodEPKv"_sym
            .hook
            ->*[]<MemBackup auto backup>(Instrumentation *thiz, ArtMethod *method,
                                         const void *new_code) static -> void {
        auto old_code = method->GetEntryPoint();
        backup(thiz, method, new_code);
        if (old_code != new_code) {
            HandleMethodCodeUpdated(method, old_code, new_code);
        }
    };

    inline static auto UpdateMethodsCode_ =
        "_ZN3art15instrumentation15Instrumentation17UpdateMethodsCodeEPNS_9ArtMethodEPKv"_sym.hook
            ->*[]<MemBackup auto backup>(Instrumentation *thiz, ArtMethod *method,
                                         const void *quick_code) static -> void {
        auto old_code = method->GetEntryPoint();
        backup(thiz, method, quick_code);
        if (old_code != quick_code) {
            HandleMethodCodeUpdated(method, old_code, quick_code);
        }
    };

    inline static auto UpdateMethodsCodeWithProtableCode_ =
        "_ZN3art15instrumentation15Instrumentation17UpdateMethodsCodeEPNS_6mirror9ArtMethodEPKvS6_b"_sym
            .hook
            ->*[]<MemBackup auto backup>(Instrumentation *thiz, ArtMethod *method,
                                         const void *quick_code, const void *portable_code,
                                         bool have_portable_code) static -> void {
        auto old_code = method->GetEntryPoint();
        backup(thiz, method, quick_code, portable_code, have_portable_code);
        if (old_code != quick_code) {
            HandleMethodCodeUpdated(method, old_code, quick_code);
        }
    };

public:
    static bool Init(JNIEnv *env, const HookHandler &handler) {
        int sdk_int = GetAndroidApiLevel();

        if (sdk_int >= __ANDROID_API_M__) [[likely]] {
            handler(UpdateMethodsCodeImpl_, UpdateMethodsCode_);
        } else {
            handler(UpdateMethodsCodeWithProtableCode_);
        }

        if (!IsJavaDebuggable(env)) [[likely]] {
            return true;
        }
        if (sdk_int >= __ANDROID_API_P__) [[likely]] {
            if (!handler(ReinitializeMethodsCode_, InitializeMethodsCode_,
                         UpdateMethodsCodeToInterpreterEntryPoint_)) {
                LOGE(
                    "Failed to hook ReinitializeMethodsCode, InitializeMethodsCode or UpdateMethodsCodeToInterpreterEntryPoint");
                return false;
            }
        }
        return true;
    }
};

}  // namespace lsplant::art
